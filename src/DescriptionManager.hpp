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
class DescriptionManager : public MessageHandler {
public:
    DescriptionManager() = default;

    // Request a description for an inventory item at the given slot.
    void requestDescription(NetworkManager& net, int invSlot,
                            const std::string& itemName);

    bool hasDescription() const { return !m_description.empty(); }
    const std::string& itemName() const { return m_itemName; }
    const std::string& description() const { return m_description; }
    void dismiss();

    void handleMessage(const json& message) override;

private:
    std::string m_itemName;
    std::string m_description;
    bool m_pending = false;

    // Strip DCSS color tags (<lightgrey>, </lightgrey>, etc.) from text.
    static std::string stripColorTags(const std::string& raw);
};
