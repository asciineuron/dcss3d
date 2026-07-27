#pragma once

#include "MessageQueue.hpp"
#include <string>
#include <vector>

class NetworkManager;

// Captures item description responses from the server.
//
// The server sends item descriptions via ui-push (type="describe-item")
// with embedded color tags (<lightgrey>, etc.) and a SPELLSET_PLACEHOLDER
// marker.  The JS client converts color tags to HTML spans and replaces
// the placeholder with a rendered spell list from the spellset array.
// We strip color tags for plain-text display and handle the placeholder
// based on spellset presence.
//
// Registered alongside UIManager for ui-push messages: UIManager manages
// the UI stack, DescriptionManager parses and caches the description text.
class DescriptionManager : public MessageHandler {
public:
    DescriptionManager() = default;

    bool hasDescription() const { return !m_description.empty(); }
    const std::string& itemName() const { return m_itemName; }
    const std::string& description() const { return m_description; }
    void dismiss();

    void handleMessage(const json& message) override;

    // Strip DCSS color tags from text.  Public so UIManagerRender can use it.
    static std::string stripColorTags(const std::string& raw);

private:
    std::string m_itemName;
    std::string m_description;
    bool m_pending = false;
};
