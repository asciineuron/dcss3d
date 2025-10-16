#include "render.h"
#include "log.h"

// TODO: add cglm/include to include path
#include "cglm/cglm.h"

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

const char shader_path[] = "/Volumes/Ext/Code/3dcss";

struct render_context {
	struct render_info *rend_info;
	SDL_GPUDevice *gpu_dev;
	SDL_GPUGraphicsPipeline *pipeline;

	// add more sophisticated grouping of buffers here
	SDL_GPUBuffer *vertex_buf;
	SDL_GPUBuffer *index_buf;
	SDL_GPUBuffer *draw_buf;
};

struct render_info rend_info;
struct render_context rend_ctx;

// for now don't support normal index:
struct face {
	Uint16 v_idx[3];
	Uint16 t_idx[3];
};

enum model_features {
	M_FEAT_NONE = 0,
	M_FEAT_VERTEX = 1 << 1,
	M_FEAT_UV = 1 << 2,
	M_FEAT_INDEX = 1 << 3
};

struct model {
	vec3 *vertices;
	vec2 *uvs;
	struct face *faces;
	size_t vertex_count;
	size_t uv_count;
	size_t face_count;
	char *name;
	enum model_features features;
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
		log_trace("(%f %f %f) ", m->vertices[i].x, m->vertices[i].y,
			  m->vertices[i].z);
	}
	log_trace("uvs:");
	for (int i = 0; i < m->uv_count; i++) {
		log_trace("(%f %f) ", m->uvs[i].u, m->uvs[i].v);
	}
	log_trace("face vertices:");
	for (int i = 0; i < m->face_count; i++) {
		log_trace("(%hu %hu %hu) ", m->faces[i].v_idx[0],
			  m->faces[i].v_idx[1], m->faces[i].v_idx[2]);
	}
}

static char *load_file(const char *file, size_t *size)
{
	*size = 0;
	if (!file || !size) {
		return NULL;
	}
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
	size_t fsize;
	char *filebuf = load_file(file, &fsize);

	struct model *model = calloc(1, sizeof(struct model));

	// ignore in return for now:
	size_t normal_count = 0;
	size_t parameter_count = 0;
	size_t line_count = 0;

	char *line = filebuf;
	int matched_vals = 0;
	// {#, v, vt, vn, vp, f, l} + '\0'
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
			--model->faces[cur_f].v_idx[0];
			--model->faces[cur_f].v_idx[1];
			--model->faces[cur_f].v_idx[2];

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
	log_trace("model upload started");
	// TODO: use model feature flags, or determine if size=0 to skip parts
	const Uint32 vertex_buf_size = sizeof(vec3) * model->vertex_count;
	log_trace("vertices %d and size %lu", model->vertex_count,
		  vertex_buf_size);
	ctx->vertex_buf = SDL_CreateGPUBuffer(
		ctx->gpu_dev,
		&(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
					    .size = vertex_buf_size });

	// 3 vertex indices per triangle face
	const Uint32 index_buf_size = sizeof(Uint16) * 3 * model->face_count;
	log_trace("faces %d and size %lu", model->face_count, index_buf_size);
	ctx->index_buf = SDL_CreateGPUBuffer(
		ctx->gpu_dev,
		&(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_INDEX,
					    .size = index_buf_size });

	const Uint32 draw_buf_size = sizeof(SDL_GPUIndexedIndirectDrawCommand) *
				     1; // only 1 draw per model
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
	// TODO: doesn't work for arrays:
	// for (int i = 0; i < model->vertex_count; ++i) {
	// 	vertex_trans[i] = model->vertices[i];
	// }
	memcpy();

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

bool render_init()
{
	rend_ctx = { .rend_info = &rend_info };

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
	SDL_GPUShader *vertex_shader =
		load_shader(rend_ctx.gpu_dev, "position.vert", 0, 1, 0, 0);
	SDL_GPUShader *frag_shader =
		load_shader(rend_ctx.gpu_dev, "color.frag", 0, 0, 0, 0);

	if (!vertex_shader || !frag_shader) {
		log_err("unable to load shaders");
		return false;
	}

	// just vertex float3 for now, no color or uv etc.
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

	rend_ctx.pipeline =
		SDL_CreateGPUGraphicsPipeline(rend_ctx.gpu_dev, &pipeline_info);
	if (!rend_ctx.pipeline) {
		log_err("pipeline creation error: %s", SDL_GetError());
		return false;
	}

	SDL_ReleaseGPUShader(rend_ctx.gpu_dev, vertex_shader);
	SDL_ReleaseGPUShader(rend_ctx.gpu_dev, frag_shader);

	// load vertex/index data:
	struct model *model = load_obj("/Volumes/Ext/Create/monkey.obj");

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

static glm::mat4 camera_to_viewproj(const struct camera *cam)
{
	// glm::mat4 view = glm::translate(glm::mat4(1.0f),
	// 				glm::vec3(-cam->x, -cam->z, cam->y));
	// glm::mat4 theta_rot = glm::rotate(glm::mat4(1.0f),
	// 				  (float)(M_PI / 2.) - cam->theta,
	// 				  glm::vec3(0.0f, 1.0f, 0.0f));
	// view = theta_rot * view;

	float cos_phi = cos(cam->phi);
	glm::vec3 lookdir = glm::vec3((float)cos(-cam->theta) * cos_phi,
				      (float)sin(cam->phi),
				      (float)sin(-cam->theta) * cos_phi);
	lookdir = glm::normalize(lookdir);

	glm::vec3 pos = glm::vec3(cam->x, cam->z, -cam->y);
	glm::mat4 lookat =
		glm::lookAt(pos, pos + lookdir, glm::vec3(0.0f, 1.0f, 0.0f));

	glm::mat4 projection =
		glm::perspective(cam->fov, cam->aspect_ratio, 0.1f, 100.0f);

	return projection * lookat;
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

		glm::mat4 camera_transform =
			camera_to_viewproj(&(game_ctx->player->camera));

		SDL_PushGPUVertexUniformData(cmd_buf, 0,
					     glm::value_ptr(camera_transform),
					     sizeof(glm::mat4));

		// TODO: update here many copies based on visible map, and push relevant gpu data
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
