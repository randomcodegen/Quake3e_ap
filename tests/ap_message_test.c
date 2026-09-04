#undef NDEBUG
#include <assert.h>
#include "APCc.c"

static void received(uint64_t item, int player, bool notify) {
    assert(item == 42 && player >= 1 && player <= 2 && notify);
}

static void check_message(const char* packet, AP_MessageType type, const char* text,
                          const char* location) {
    json_error_t error;
    json_t* root = json_loads(packet, 0, &error);
    struct AP_Message* message;
    int location_parts = 0, item_parts = 0;
    assert(root);
    parse_response(root);
    json_decref(root);
    assert(g_queue_get_length(messageQueue) == 1);
    message = g_queue_pop_head(messageQueue);
    assert(message->type == type && !strcmp(message->text, text));
    for (guint i = 0; i < message->messageParts->len; ++i) {
        struct AP_MessagePart* part = g_array_index(message->messageParts, struct AP_MessagePart*, i);
        if (part->type == AP_LocationText) {
            assert(location && !strcmp(part->text, location));
            ++location_parts;
        }
        if (part->type == AP_ItemText) { assert(part->flags == 1); ++item_parts; }
        AP_MessagePart_free(part);
    }
    assert(location_parts == (location ? 1 : 0) && item_parts == 1);
    if (type == ItemRecv) AP_ItemRecvMessage_free((struct AP_ItemRecvMessage*)message);
    else AP_ItemSendMessage_free((struct AP_ItemSendMessage*)message);
}

int main(void) {
    const char* names[] = { "Archipelago", "Ranger", "OtherPlayer" };
    const char* games[] = { "__Server", "Quake III Arena", "Other Game" };
    json_error_t error;
    ap_player_id = 1; ap_game = games[1]; ap_player_name = names[1];
    multiworld = false; getitemfunc = received;
    messageQueue = g_queue_new();
    map_players = g_array_new(true, true, sizeof(struct AP_NetworkPlayer*));
    for (int i = 0; i < 3; ++i) {
        struct AP_NetworkPlayer* player = AP_NetworkPlayer_new(0, i, names[i], names[i], games[i]);
        g_array_append_val(map_players, player);
    }
    datapkg_cache = json_loads("{\"games\":{"
        "\"Quake III Arena\":{\"item_name_to_id\":{\"Rocket Launcher Unlock\":42},\"location_name_to_id\":{\"Q3 Armor Shard\":101}},"
        "\"Other Game\":{\"item_name_to_id\":{\"Foreign Item\":42},\"location_name_to_id\":{\"Foreign Castle\":101}}}}", 0, &error);
    assert(datapkg_cache);
    parseDataPkgCache();
    check_message("[{\"cmd\":\"ReceivedItems\",\"index\":1,\"items\":[{\"item\":42,\"player\":2,\"location\":101,\"flags\":1}]}]",
        ItemRecv, "Received Rocket Launcher Unlock from OtherPlayer (Foreign Castle)", "Foreign Castle");
    check_message("[{\"cmd\":\"ReceivedItems\",\"index\":2,\"items\":[{\"item\":42,\"player\":1,\"location\":101,\"flags\":1}]}]",
        ItemRecv, "Received Rocket Launcher Unlock from Ranger (Q3 Armor Shard)", "Q3 Armor Shard");
    check_message("[{\"cmd\":\"PrintJSON\",\"type\":\"ItemSend\",\"receiving\":2,\"item\":{\"item\":42,\"player\":1,\"location\":101,\"flags\":1}}]",
        ItemSend, "Foreign Item was sent to OtherPlayer (Q3 Armor Shard)", "Q3 Armor Shard");
    check_message("[{\"cmd\":\"ReceivedItems\",\"index\":3,\"items\":[{\"item\":42,\"player\":2,\"flags\":1}]}]",
        ItemRecv, "Received Rocket Launcher Unlock from OtherPlayer", NULL);
    puts("PASS: received/sent locations, source-game lookup, flags, and missing location.");
    return 0;
}
