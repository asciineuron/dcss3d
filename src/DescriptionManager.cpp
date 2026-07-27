#include "DescriptionManager.hpp"
#include "MessageQueue.hpp"
#include <algorithm>
#include <cctype>
#include <spdlog/spdlog.h>

void DescriptionManager::dismiss()
{
    m_itemName.clear();
    m_description.clear();
    m_pending = false;
}

// Strip DCSS color tags from a string.  The server uses tags like
// "<lightgrey>" and "</lightgrey>" to indicate color changes.  The JS
// client converts these to HTML <span> elements via formatted_string_to_html().
// We remove them for plain-text display.
//
// Only strips properly-formed <tagname> and </tagname> where tagname is
// alphanumeric (optionally prefixed with "bg:" for background colors).
// Other angle brackets (e.g. "<<" or ">") are left alone.
std::string DescriptionManager::stripColorTags(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());

    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '<') {
            // Check if this looks like a color tag: <alpha...> or </alpha...>
            size_t j = i + 1;
            bool closing = false;
            if (j < raw.size() && raw[j] == '/') {
                closing = true;
                ++j;
            }
            // Optional "bg:" prefix
            if (j + 3 <= raw.size() && raw.substr(j, 3) == "bg:") {
                j += 3;
            }
            // Tag name must start with a letter
            if (j < raw.size() && std::isalpha(static_cast<unsigned char>(raw[j]))) {
                // Find the closing '>'
                size_t close = raw.find('>', j);
                if (close != std::string::npos) {
                    // Verify tag content is alphanumeric (e.g. "lightgrey")
                    bool validTag = true;
                    for (size_t k = j; k < close; ++k) {
                        if (!std::isalnum(static_cast<unsigned char>(raw[k]))) {
                            validTag = false;
                            break;
                        }
                    }
                    if (validTag) {
                        i = close; // skip the entire tag (loop increment moves past '>')
                        continue;
                    }
                }
            }
        }
        out += raw[i];
    }

    return out;
}

void DescriptionManager::handleMessage(const json& message)
{
    const std::string msgType = message.value("msg", "");

    if (msgType == "ui-push"
        && message.value("type", "") == "describe-item") {

        m_itemName = message.value("title", m_itemName);

        // Get the raw body with embedded color tags
        std::string rawBody = message.value("body", "");

        // Strip color tags: <lightgrey> → "", </lightgrey> → ""
        std::string clean = stripColorTags(rawBody);

        // Handle SPELLSET_PLACEHOLDER — the server marks where spell
        // data belongs.  The JS client replaces it with a <div> and
        // populates it from the spellset array.  For items without
        // spells (empty spellset), the <div> is removed entirely.
        //
        // We do the same: if spellset is empty, remove the placeholder.
        // If non-empty, we'd format the spells — for now just note it.
        size_t placeholderPos = clean.find("SPELLSET_PLACEHOLDER");
        if (placeholderPos != std::string::npos) {
            // Check if the ui-push has a non-empty spellset
            bool hasSpells = false;
            if (auto ss = message.find("spellset"); ss != message.end() && ss->is_array()) {
                hasSpells = !ss->empty();
            }

            if (hasSpells) {
                // TODO: format spells from the spellset array.
                // For now, replace with a note.
                clean.replace(placeholderPos, 20, "[Spells listed in game]");
            } else {
                // No spells — remove the placeholder and surrounding whitespace.
                clean.erase(placeholderPos, 20);
                // Clean up any double-newlines left behind
                size_t pos;
                while ((pos = clean.find("\n\n\n")) != std::string::npos) {
                    clean.erase(pos, 1);
                }
            }
        }

        m_description = clean;
        m_pending = false;
        spdlog::info("DescriptionManager: received '{}' ({} chars)",
            m_itemName, m_description.size());

    } else if (msgType == "ui-pop") {
        m_pending = false;
    }
}
