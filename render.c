#include "render.h"
#include "log.h"

#include <limits.h>
#include <math.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define WIN_W 1920
#define WIN_H 1080

// aspect ratio may warrant unequal x and y sensitivities
#define MOUSE_SENSITIVITY_X 0.005
#define MOUSE_SENSITIVITY_Y 0.005

#define FOV_RAD 0.785398
// TODO for mouse fix for x vs y?
#define FOV_DEG 45
#define ASPECT 1.777777

char shader_path[PATH_MAX];
char resource_path[PATH_MAX];

static const vec4 map_type_color[MTYPE_COUNT] = {
	[MTYPE_WALL] = { 1.0f, 0.0f, 0.0f, 1.0f },
	[MTYPE_FLOOR] = { 0.0f, 1.0f, 0.0f, 1.0f },
	[MTYPE_UNKNOWN] = { 0.0f, 0.0f, 1.0f, 1.0f },
};

struct camera {
	vec3 pos; // x,y,z
	float fov;
	float aspect_ratio;
	float theta;
	float phi;
};

struct render_context {
	struct camera camera;

	struct render_info *rend_info;
	SDL_GPUDevice *gpu_dev;
	SDL_GPUGraphicsPipeline *pipeline;

	// add more sophisticated grouping of buffers here
	SDL_GPUBuffer *vertex_buf;
	SDL_GPUBuffer *index_buf;
	SDL_GPUBuffer *draw_buf;
	SDL_GPUBuffer *map_data_buf;
};

struct render_info rend_info;
struct render_context rend_ctx;

// for now don't support normal index:
struct face {
	Uint16 v_idx[3];
	Uint16 t_idx[3];
};

enum model_type {
	MODEL_ACTOR, // default
	MODEL_MAP,
	MODEL_COUNT
};

struct model {
	vec3 *vertices;
	vec2 *uvs;
	struct face *faces;
	size_t vertex_count;
	size_t uv_count;
	size_t face_count;
	char *name;
	enum model_type type;
};

static void free_model(struct model *m)
{
	if (!m)
		return;
	free(m->vertices);
	free(m->uvs);
	free(m->faces);
	free(m->name);
	free(m);
}

void print_model(const struct model *m)
{
	log_trace("printing model %s", m->name);
	log_trace("vertices:");
	for (int i = 0; i < m->vertex_count; i++) {
		log_trace("(%f %f %f) ", m->vertices[i][0], m->vertices[i][1],
			  m->vertices[i][2]);
	}
	log_trace("uvs:");
	for (int i = 0; i < m->uv_count; i++) {
		log_trace("(%f %f) ", m->uvs[i][0], m->uvs[i][1]);
	}
	log_trace("face vertices:");
	for (int i = 0; i < m->face_count; i++) {
		log_trace("(%hu %hu %hu) ", m->faces[i].v_idx[0],
			  m->faces[i].v_idx[1], m->faces[i].v_idx[2]);
	}
}

static char *load_file(const char *file, size_t *size)
{
	if (!file || !size) {
		return NULL;
	}
	*size = 0;
	FILE *fp = fopen(file, "r");
	if (!fp) {
		log_err("fopen error for %s", file);
		return NULL;
	}

	struct stat st;
	if (fstat(fileno(fp), &st) == -1) {
		perror("fstat error\n");
		fclose(fp);
		return NULL;
	}

	char *filebuf = malloc(st.st_size + 1);
	if (fread(filebuf, st.st_size, 1, fp) != 1) {
		log_err("fread error");
		free(filebuf);
		filebuf = NULL;
	} else {
		filebuf[st.st_size] = '\0';
		*size = st.st_size;
	}
	fclose(fp);

	return filebuf;
}

// load full buffer
// readline with strchr('\n')
// find first word in line, use small word buffer, categorize as {#, v, vt, vn, vp, f, l}
// count nums of each element, malloc structures
// re-read, parse into data structures
static struct model *load_obj(const char *file)
{
	char full_file[PATH_MAX];
	snprintf(full_file, PATH_MAX, "%s/%s", resource_path, file);
	size_t fsize;
	char *filebuf = load_file(full_file, &fsize);

	struct model *model = calloc(1, sizeof(struct model));

	// ignore in return for now:
	size_t normal_count = 0;
	size_t parameter_count = 0;
	size_t line_count = 0;

	char *line = filebuf;
	int matched_vals = 0;
	// typeword in: {#, v, vt, vn, vp, f, l} + '\0'
	char typeword[3] = { 0 };
	do {
		matched_vals = sscanf(line, "%2s", typeword);
		if (matched_vals == EOF || matched_vals < 1) {
			log_err("fprintf err, line");
			free(filebuf);
			free_model(model);
			return NULL;
		}

		if (strcmp(typeword, "v") == 0) {
			model->vertex_count++;
		} else if (strcmp(typeword, "vt") == 0) {
			model->uv_count++;
		} else if (strcmp(typeword, "vn") == 0) {
			++normal_count;
		} else if (strcmp(typeword, "vp") == 0) {
			++parameter_count;
		} else if (strcmp(typeword, "f") == 0) {
			// TODO: for now expect triangular faces e.g. f # # #,
			// f #/# #/# #/#, f #/#/# #/#/# #/#/#, or f #//# #//# #//#
			model->face_count++;
		} else if (strcmp(typeword, "l") == 0) {
			++line_count;
		} else if (strcmp(typeword, "#") == 0) {
			int line_len = strchr(line, '\n') - line;
			log_trace("read comment: %.*s", line_len, line);
		} else {
			continue;
		}
	} while ((line = strchr(line, '\n')) != NULL && *(++line) != '\0');

	model->vertices = calloc(model->vertex_count, sizeof(vec3));
	model->uvs = calloc(model->uv_count, sizeof(vec2));
	model->faces = calloc(model->face_count, sizeof(struct face));

	size_t cur_v = 0;
	size_t cur_f = 0;
	size_t cur_tex = 0;

	line = filebuf;
	int typewordlen;
	do {
		matched_vals = sscanf(line, "%2s%n", typeword, &typewordlen);
		if (matched_vals == EOF || matched_vals < 1) {
			log_err("fprintf err");
			free(filebuf);
			free_model(model);
			return NULL;
		}
		line += typewordlen;

		if (strcmp(typeword, "v") == 0) {
			sscanf(line, "%f %f %f", &(model->vertices[cur_v][0]),
			       &(model->vertices[cur_v][1]),
			       &(model->vertices[cur_v][2]));
			++cur_v;
		} else if (strcmp(typeword, "vt") == 0) {
			sscanf(line, "%f %f", &(model->uvs[cur_tex][0]),
			       &(model->uvs[cur_tex][1]));
			++cur_tex;
		} else if (strcmp(typeword, "f") == 0) {
			// NOTE: the obj format is 1-indexed. -1 is the last element etc. Need to correct to 0-indexed here

			// TODO: for now expect triangular faces e.g. f # # #, f #/# #/# #/#,
			// f #/#/# #/#/# #/#/#, or f #//# #//# #//#
			// how to separate 1 2 3 vs 1/# 2/# 3/# ?
			// TODO: if every line is of same type, only need to determine this
			// once vs trying all 3 for each vertex.

			// for # # #:
			matched_vals = sscanf(line, "%hu %hu %hu",
					      &(model->faces[cur_f].v_idx[0]),
					      &(model->faces[cur_f].v_idx[1]),
					      &(model->faces[cur_f].v_idx[2]));
			if (matched_vals < 3) {
				// for #/# #/# #/#:
				matched_vals =
					sscanf(line, "%hu/%hu %hu/%hu %hu/%hu",
					       &(model->faces[cur_f].v_idx[0]),
					       &(model->faces[cur_f].t_idx[0]),
					       &(model->faces[cur_f].v_idx[1]),
					       &(model->faces[cur_f].t_idx[1]),
					       &(model->faces[cur_f].v_idx[2]),
					       &(model->faces[cur_f].t_idx[2]));
				if (matched_vals < 6) {
					int line_len =
						strchr(line, '\n') - line;
					log_warn(
						"currently don't support face formats beyond # # #,"
						" and #/# #/# #/#, got %.*s",
						line_len, line);
				}
			}

			// switch to 0-based indexing
			--(model->faces[cur_f].v_idx[0]);
			--(model->faces[cur_f].v_idx[1]);
			--(model->faces[cur_f].v_idx[2]);

			++cur_f;
			// } else if (strcmp(typeword, "vn") == 0) {
			// 	continue;
			// } else if (strcmp(typeword, "vp") == 0) {
			// 	continue;
			// } else if (strcmp(typeword, "l") == 0) {
			// 	continue;
			// } else if (strcmp(typeword, "#") == 0) {
			// 	continue;
		} else {
			continue;
		}
	} while ((line = strchr(line, '\n')) != NULL && *(++line) != '\0');
	free(filebuf);

	if (cur_v != model->vertex_count || cur_tex != model->uv_count ||
	    cur_f != model->face_count) {
		log_err("could not correctly load all model data from %s",
			file);
		free_model(model);
		return NULL;
	}

	log_trace(
		"loaded model %s containing %ld vertices, %ld texture coords, and %ld faces\n",
		file, model->vertex_count, model->uv_count, model->face_count);
	model->name = strdup(file);
	print_model(model);
	return model;
}

static SDL_GPUShader *load_shader(SDL_GPUDevice *device, const char *filename,
				  Uint32 sampler_count,
				  Uint32 uniform_buffer_count,
				  Uint32 storage_buffer_count,
				  Uint32 storage_texture_count)
{
	SDL_GPUShaderStage stage;
	if (strstr(filename, ".vert")) {
		stage = SDL_GPU_SHADERSTAGE_VERTEX;
	} else if (strstr(filename, ".frag")) {
		stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	} else {
		log_err("invalid shader stage");
		return NULL;
	}

	SDL_GPUShaderFormat backend_formats = SDL_GetGPUShaderFormats(device);
	SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
	const char *entrypoint;
	const char *extension;
	char full_shader_path[PATH_MAX];

	int pos = snprintf(full_shader_path, PATH_MAX, "%s/%s", shader_path,
			   filename);

	if (backend_formats & SDL_GPU_SHADERFORMAT_SPIRV) {
		format = SDL_GPU_SHADERFORMAT_SPIRV;
		extension = ".spv";
		entrypoint = "main";
	} else if (backend_formats & SDL_GPU_SHADERFORMAT_MSL) {
		format = SDL_GPU_SHADERFORMAT_MSL;
		extension = ".msl";
		entrypoint = "main0";
	} else if (backend_formats & SDL_GPU_SHADERFORMAT_DXIL) {
		format = SDL_GPU_SHADERFORMAT_DXIL;
		extension = ".dxil";
		entrypoint = "main";
	} else {
		log_err("unrecognized backend shader format");
		return NULL;
	}
	snprintf(full_shader_path + pos, PATH_MAX - pos, "%s", extension);

	size_t fsize;
	char *code = load_file(full_shader_path, &fsize);
	if (!code) {
		log_err("load_file error");
		return NULL;
	}

	SDL_GPUShaderCreateInfo shader_info = {
		.code = (Uint8 *)code,
		.code_size = fsize,
		.entrypoint = entrypoint,
		.format = format,
		.stage = stage,
		.num_samplers = sampler_count,
		.num_uniform_buffers = uniform_buffer_count,
		.num_storage_buffers = storage_buffer_count,
		.num_storage_textures = storage_texture_count
	};
	SDL_GPUShader *shader = SDL_CreateGPUShader(device, &shader_info);
	if (!shader) {
		log_err("failed to create shader");
	}

	log_info("loaded shader %s", full_shader_path);
	free(code);
	return shader;
}

static bool upload_model(struct render_context *ctx, const struct model *model)
{
	// TODO: use model feature flags, or determine if size=0 to skip parts
	log_trace("model upload started");

	const Uint32 vertex_buf_size = sizeof(vec3) * model->vertex_count;
	ctx->vertex_buf = SDL_CreateGPUBuffer(
		ctx->gpu_dev,
		&(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
					    .size = vertex_buf_size });
	log_trace("vertices %d and size %lu", model->vertex_count,
		  vertex_buf_size);

	// 3 vertex indices per triangle face
	const Uint32 index_buf_size = sizeof(Uint16) * 3 * model->face_count;
	ctx->index_buf = SDL_CreateGPUBuffer(
		ctx->gpu_dev,
		&(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_INDEX,
					    .size = index_buf_size });
	log_trace("faces %d and size %lu", model->face_count, index_buf_size);

	// only 1 draw per model
	const Uint32 draw_buf_size =
		sizeof(SDL_GPUIndexedIndirectDrawCommand) * 1;
	ctx->draw_buf = SDL_CreateGPUBuffer(
		ctx->gpu_dev, &(SDL_GPUBufferCreateInfo){
				      .usage = SDL_GPU_BUFFERUSAGE_INDIRECT,
				      .size = draw_buf_size });

	SDL_GPUTransferBuffer *trans_buf = SDL_CreateGPUTransferBuffer(
		ctx->gpu_dev,
		&(SDL_GPUTransferBufferCreateInfo){
			.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
			.size = vertex_buf_size + index_buf_size +
				draw_buf_size });

	vec3 *vertex_trans = (vec3 *)SDL_MapGPUTransferBuffer(ctx->gpu_dev,
							      trans_buf, false);
	memcpy(vertex_trans, model->vertices,
	       model->vertex_count * sizeof(vec3));

	// or memcpy
	Uint16 *index_trans = (Uint16 *)&vertex_trans[model->vertex_count];
	for (int i = 0; i < model->face_count; i++) {
		index_trans[3 * i] = model->faces[i].v_idx[0];
		index_trans[3 * i + 1] = model->faces[i].v_idx[1];
		index_trans[3 * i + 2] = model->faces[i].v_idx[2];
	}

	SDL_GPUIndexedIndirectDrawCommand *draw_trans =
		(SDL_GPUIndexedIndirectDrawCommand
			 *)&index_trans[3 * model->face_count];
	// TODO: for wall, have more num_instances
	draw_trans[0] = (SDL_GPUIndexedIndirectDrawCommand){
		.num_indices = (Uint32)(3 * model->face_count),
		.num_instances = 1,
		.first_index = 0,
		.vertex_offset = 0,
		.first_instance = 0
	};

	SDL_UnmapGPUTransferBuffer(ctx->gpu_dev, trans_buf);

	SDL_GPUCommandBuffer *cmd_buf =
		SDL_AcquireGPUCommandBuffer(ctx->gpu_dev);
	SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd_buf);

	SDL_UploadToGPUBuffer(
		copy_pass,
		&(SDL_GPUTransferBufferLocation){ .transfer_buffer = trans_buf,
						  .offset = 0 },
		&(SDL_GPUBufferRegion){ .buffer = ctx->vertex_buf,
					.offset = 0,
					.size = vertex_buf_size },
		false);
	SDL_UploadToGPUBuffer(
		copy_pass,
		&(SDL_GPUTransferBufferLocation){ .transfer_buffer = trans_buf,
						  .offset = vertex_buf_size },
		&(SDL_GPUBufferRegion){ .buffer = ctx->index_buf,
					.offset = 0,
					.size = index_buf_size },
		false);

	SDL_UploadToGPUBuffer(
		copy_pass,
		&(SDL_GPUTransferBufferLocation){ .transfer_buffer = trans_buf,
						  .offset = vertex_buf_size +
							    index_buf_size },
		&(SDL_GPUBufferRegion){ .buffer = ctx->draw_buf,
					.offset = 0,
					.size = draw_buf_size },
		false);

	SDL_EndGPUCopyPass(copy_pass);
	SDL_SubmitGPUCommandBuffer(cmd_buf);
	SDL_ReleaseGPUTransferBuffer(rend_ctx.gpu_dev, trans_buf);

	log_trace("model uploaded");
	return true;
}

SDL_GPUGraphicsPipeline *create_graphics_pipeline(SDL_GPUDevice *device,
						  enum model_type type)
{
	// for now MODEL_ACTOR and MODEL_MAP have same pipeline except shaders used, since
	// pipeline doesn't show the cbuffer shader information that distinguishes them
	SDL_GPUGraphicsPipeline *pipeline;
	SDL_GPUShader *vertex_shader;
	SDL_GPUShader *frag_shader;

	switch (type) {
	case MODEL_ACTOR: {
		vertex_shader = load_shader(rend_ctx.gpu_dev, "position.vert",
					    0, 1, 0, 0);
		frag_shader =
			load_shader(rend_ctx.gpu_dev, "color.frag", 0, 0, 0, 0);
		break;
	}
	case MODEL_MAP: {
		vertex_shader = load_shader(rend_ctx.gpu_dev,
					    "position_color_shifted.vert", 0, 1,
					    0, 0);
		frag_shader = load_shader(rend_ctx.gpu_dev,
					  "vert_input_color.frag", 0, 0, 0, 0);
		break;
	}
	default:
		break;
	}

	if (!vertex_shader || !frag_shader) {
		log_err("unable to load shaders");
		return NULL;
	}

	SDL_GPUVertexAttribute vertex_attributes[] = {
		{ .buffer_slot = 0,
		  .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
		  .location = 0,
		  .offset = 0 }
	};
	SDL_GPUVertexBufferDescription vertex_buffer_descriptions[] = {
		{ .slot = 0,
		  .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
		  .instance_step_rate = 0,
		  .pitch = sizeof(vec3) }
	};
	// TODO add more vertex buffers here, one for map, one for each actor etc?
	SDL_GPUVertexInputState vertex_input_state = {
		.num_vertex_buffers = 1,
		.vertex_buffer_descriptions = vertex_buffer_descriptions,
		.num_vertex_attributes = 1,
		.vertex_attributes = vertex_attributes,
	};
	SDL_GPUColorTargetDescription color_target_descriptions[] = {
		{ .format = SDL_GetGPUSwapchainTextureFormat(
			  rend_ctx.gpu_dev, rend_ctx.rend_info->window) }
	};
	SDL_GPURasterizerState rasterizer_state = {
		.fill_mode = SDL_GPU_FILLMODE_LINE,
		.cull_mode = SDL_GPU_CULLMODE_NONE
	};
	SDL_GPUGraphicsPipelineCreateInfo
		pipeline_info = { .vertex_shader = vertex_shader,
				  .fragment_shader = frag_shader,
				  .vertex_input_state = vertex_input_state,
				  .primitive_type =
					  SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
				  .rasterizer_state = rasterizer_state,
				  .target_info = {
					  .color_target_descriptions =
						  color_target_descriptions,
					  .num_color_targets = 1,
				  } };

	pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipeline_info);

	SDL_ReleaseGPUShader(rend_ctx.gpu_dev, vertex_shader);
	SDL_ReleaseGPUShader(rend_ctx.gpu_dev, frag_shader);

	return pipeline;
}

static struct gpu_map_pos_info {
	uint8_t pos_x, pos_y;
	vec4 color;
};

bool render_init()
{
	// set up resource+shader dirs
	log_trace("using base dir: %s", SDL_GetBasePath());
	snprintf(resource_path, PATH_MAX, "%s%s", SDL_GetBasePath(),
		 "resources");
	snprintf(shader_path, PATH_MAX, "%s%s", SDL_GetBasePath(), "shaders");
	log_trace("using resource dir: %s", resource_path);
	log_trace("using shader dir: %s", shader_path);

	rend_ctx = (struct render_context) { 
		.camera = { 
			.pos = GLM_VEC3_ZERO_INIT,
			.fov = FOV_RAD,
			.aspect_ratio = ASPECT,
			.theta = 0,
			.phi = 0,
		},
		.rend_info = &rend_info, 
		0 
	};

	// create window:
	// 200% for retina TODO: is this needed for w, h in CreateWindow,
	// or does it take into account already?
	float display_scale =
		SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
	SDL_WindowFlags win_flags = SDL_WINDOW_RESIZABLE |
				    SDL_WINDOW_HIGH_PIXEL_DENSITY;

	rend_ctx.rend_info->window =
		SDL_CreateWindow("sdlproj1", (int)(WIN_W * display_scale),
				 (int)(WIN_H * display_scale), win_flags);
	if (!rend_ctx.rend_info->window) {
		log_err("SDL_CreateWindow failed: %s", SDL_GetError());
		return false;
	}

	rend_ctx.rend_info->window_id =
		SDL_GetWindowID(rend_ctx.rend_info->window);

	if (!SDL_GetWindowSize(rend_ctx.rend_info->window,
			       &rend_ctx.rend_info->win_w,
			       &rend_ctx.rend_info->win_h)) {
		log_err("SDL_GetWindowSize error: %s", SDL_GetError());
	}

	// SDL gpu setup:
	rend_ctx.gpu_dev = SDL_CreateGPUDevice(
		SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV |
			SDL_GPU_SHADERFORMAT_DXIL,
		true, NULL);
	if (!rend_ctx.gpu_dev) {
		log_err("SDL_CreateGPUDevice failed: %s", SDL_GetError());
		return false;
	}

	if (!SDL_ClaimWindowForGPUDevice(rend_ctx.gpu_dev,
					 rend_ctx.rend_info->window)) {
		log_err("SDL_ClaimWindowForGPUDevice failed: %s",
			SDL_GetError());
		return false;
	}

	// enable vsync and (default) sdr
	if (!SDL_SetGPUSwapchainParameters(rend_ctx.gpu_dev,
					   rend_ctx.rend_info->window,
					   SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
					   SDL_GPU_PRESENTMODE_VSYNC)) {
		log_err("SDL_SetGPUSwapchainParameters failed: %s",
			SDL_GetError());
		return false;
	}

	// load models and shaders etc etc
	rend_ctx.pipeline =
		create_graphics_pipeline(rend_ctx.gpu_dev, MODEL_ACTOR);
	if (!rend_ctx.pipeline) {
		log_err("pipeline creation error: %s", SDL_GetError());
		return false;
	}

	// load vertex/index data:
	// struct model *model = load_obj("monkey.obj");
	struct model *model = load_obj("cube.obj");
	model->type = MODEL_MAP;

	// set up gpu buffer for map data:
	rend_ctx.map_data_buf = SDL_CreateGPUBuffer(
		rend_ctx.gpu_dev,
		&(SDL_GPUBufferCreateInfo){
			.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
			.size = MAX_MAP_VISIBLE *
				sizeof(struct gpu_map_pos_info) });

	// struct vec3 square_v[4] = {
	// 	{ -1, -1, 0 }, { 1, -1, 0 }, { 1, 1, 0 }, { -1, 1, 0 }
	// };
	// struct face square_f[2] = { { { 0, 1, 2 }, { 0, 0, 0 } },
	// 			    { { 0, 2, 3 }, { 0, 0, 0 } } };
	// struct model testmodel = { .vertices = square_v,
	// 			   .uvs = NULL,
	// 			   .faces = square_f,
	// 			   .vertex_count = 4,
	// 			   .uv_count = 0,
	// 			   .face_count = 2 };
	// print_model(&testmodel);
	upload_model(&rend_ctx, model);
	free(model);
	return true;
}

static void camera_to_viewproj(const struct camera *cam, mat4 dest)
{
	float cos_phi = cos(cam->phi);
	vec3 lookdir = { (float)cos(-cam->theta) * cos_phi,
			 (float)sin(cam->phi),
			 (float)sin(-cam->theta) * cos_phi };
	glm_vec3_normalize(lookdir);

	// TODO: flip y definition instead
	// ^ did this
	vec3 center;
	glm_vec3_add(cam->pos, lookdir, center);
	mat4 lookat;
	glm_lookat(cam->pos, center, (vec3){ 0.0f, 1.0f, 0.0f }, lookat);

	mat4 projection;
	// glm_perspective(cam->fov, cam->aspect_ratio, 0.1f, 100.0f, projection);
	glm_perspective_default(cam->aspect_ratio, projection);

	glm_mat4_mul(projection, lookat, dest);
}

bool push_gpu_map_data(const struct map_pos_info *visible_map)
{
	// TODO pass in *visible_map size?
	// read in list of map data, push to gpu buffer as coords

	return true;
}

bool render_draw(const struct game_context *game_ctx)
{
	// if (in_menu) {
	// 	draw_menus();
	// } else {
	// 	draw_map();
	// 	draw_entities();
	// 	draw_ui_overlay();
	// }

	SDL_GPUCommandBuffer *cmd_buf =
		SDL_AcquireGPUCommandBuffer(rend_ctx.gpu_dev);

	SDL_GPUTexture *swapchain_texture = NULL;
	SDL_WaitAndAcquireGPUSwapchainTexture(cmd_buf,
					      rend_ctx.rend_info->window,
					      &swapchain_texture, NULL, NULL);

	if (swapchain_texture) {
		SDL_GPUColorTargetInfo color_target_info = { 0 };
		color_target_info.texture = swapchain_texture;
		color_target_info.clear_color =
			(SDL_FColor){ 0.0f, 0.0f, 0.0f, 1.0f };
		color_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
		color_target_info.store_op = SDL_GPU_STOREOP_STORE;

		SDL_GPURenderPass *rend_pass = SDL_BeginGPURenderPass(
			cmd_buf, &color_target_info, 1, NULL);

		SDL_BindGPUGraphicsPipeline(rend_pass, rend_ctx.pipeline);
		SDL_BindGPUVertexBuffers(
			rend_pass, 0,
			&(SDL_GPUBufferBinding){ .buffer = rend_ctx.vertex_buf,
						 .offset = 0 },
			1);
		SDL_BindGPUIndexBuffer(
			rend_pass,
			&(SDL_GPUBufferBinding){ .buffer = rend_ctx.index_buf },
			SDL_GPU_INDEXELEMENTSIZE_16BIT);

		// use similar to displace items in scene later
		// glm::mat4 model = glm::rotate(
		// 	glm::mat4(1.0f), SDL_sinf((milliseconds / 1000.0f)),
		// 	glm::vec3(0.5f, 0.2f, 0.1f));

		mat4 camera_transform;
		camera_to_viewproj(&(rend_ctx.camera), camera_transform);

		SDL_PushGPUVertexUniformData(cmd_buf, 0, camera_transform,
					     sizeof(mat4));

		// TODO: update here many copies based on visible map, and push relevant gpu data
		if (!push_gpu_map_data(game_ctx->visible_map)) {
			log_err("push_gpu_map_data failed");
		}

		SDL_DrawGPUIndexedPrimitivesIndirect(rend_pass,
						     rend_ctx.draw_buf, 0, 1);

		SDL_EndGPURenderPass(rend_pass);
	}

	SDL_SubmitGPUCommandBuffer(cmd_buf);
	return true;
}

void render_quit()
{
	SDL_ReleaseWindowFromGPUDevice(rend_ctx.gpu_dev,
				       rend_ctx.rend_info->window);
	SDL_DestroyGPUDevice(rend_ctx.gpu_dev);
	SDL_DestroyWindow(rend_ctx.rend_info->window);
}

static float wrap(float x, float min, float max)
{
	if (min > max)
		return wrap(x, max, min);
	return (x >= 0 ? min : max) + fmod(x, max - min);
}

void camera_update_from_mouse(float mouse_dx, float mouse_dy)
{
	rend_ctx.camera.theta -= (mouse_dx * MOUSE_SENSITIVITY_X);
	rend_ctx.camera.phi -= (mouse_dy * MOUSE_SENSITIVITY_Y);

	// wraparound into [0, 2pi] range:
	rend_ctx.camera.theta = wrap(rend_ctx.camera.theta, 0, 2 * M_PI);
	rend_ctx.camera.phi = wrap(rend_ctx.camera.phi, 0, 2 * M_PI);
}

void camera_update_pos(double dt, float vx, float vy)
{
	float dx = vy * cos(rend_ctx.camera.theta) +
		   vx * cos(M_PI_2 - rend_ctx.camera.theta);
	float dy = -vy * cos(M_PI_2 - rend_ctx.camera.theta) +
		   vx * cos(rend_ctx.camera.theta);
	rend_ctx.camera.pos[0] += dt * dx;
	rend_ctx.camera.pos[2] += dt * dy;
}
