#include "pumpkin_api.h"
#include <stdio.h>
#include <stdlib.h>

// UUIDs stay in memory across reconnects, but not across server/plugin restarts.
static pumpkin_plugin_player_uuid_t *frozen_player_ids = NULL;
static size_t frozen_player_count = 0;
static size_t frozen_player_capacity = 0;

static bool same_player_id(pumpkin_plugin_player_uuid_t left,
                           pumpkin_plugin_player_uuid_t right) {
    return left.high == right.high && left.low == right.low;
}

static bool is_player_frozen(pumpkin_plugin_player_uuid_t player_id) {
    for (size_t i = 0; i < frozen_player_count; i++) {
        if (same_player_id(frozen_player_ids[i], player_id)) {
            return true;
        }
    }
    return false;
}

// Release the in-memory list when Pumpkin unloads this plugin.
static void on_unload(plugin_own_context_t ctx) {
    (void)ctx;
    free(frozen_player_ids);
    frozen_player_ids = NULL;
    frozen_player_count = 0;
    frozen_player_capacity = 0;
}

// Add/remove a UUID from the frozen list. The list grows as needed.
static bool set_player_frozen(pumpkin_plugin_player_uuid_t player_id,
                              bool frozen) {
    for (size_t i = 0; i < frozen_player_count; i++) {
        if (same_player_id(frozen_player_ids[i], player_id)) {
            if (!frozen) {
                frozen_player_ids[i] =
                    frozen_player_ids[frozen_player_count - 1];
                frozen_player_count--;
            }
            return true;
        }
    }

    if (!frozen) {
        return true;
    }

    if (frozen_player_count == frozen_player_capacity) {
        size_t max_capacity = ((size_t)-1) / sizeof(*frozen_player_ids);
        if (frozen_player_capacity > max_capacity / 2) {
            return false;
        }
        size_t new_capacity = frozen_player_capacity == 0
                                  ? 16
                                  : frozen_player_capacity * 2;
        if (new_capacity > max_capacity) {
            return false;
        }
        pumpkin_plugin_player_uuid_t *new_ids = realloc(
            frozen_player_ids, new_capacity * sizeof(*frozen_player_ids));
        if (new_ids == NULL) {
            return false;
        }
        frozen_player_ids = new_ids;
        frozen_player_capacity = new_capacity;
    }

    frozen_player_ids[frozen_player_count++] = player_id;
    return true;
}

// Reapply the movement lock after a reconnect.
static void handle_player_join(plugin_own_server_instance_t server,
                              plugin_event_t *event) {
    (void)server;

    pumpkin_plugin_player_borrow_player_t player =
        pumpkin_plugin_player_borrow_player(
            event->val.player_join_event.player);
    pumpkin_plugin_player_uuid_t player_id;
    pumpkin_plugin_player_method_player_get_id(player, &player_id);
    if (is_player_frozen(player_id)) {
        pumpkin_plugin_player_method_player_set_movement_locked(player, true);
    }
}

// Plugin metadata
static const char *authors[] = {"Zerorifter"};
static const char *permissions[] = {"slashfreeze:freeze"};

static pumpkin_metadata_t get_meta(void) {
    return (pumpkin_metadata_t) {
        .name = "slashfreeze",
        .version = "0.1.0",
        .authors = authors,
        .authors_count = sizeof(authors) / sizeof(authors[0]),
        .description = "Freeze players!",
        .dependencies_count = 0,
        .permissions = permissions,
        .permissions_count = sizeof(permissions) / sizeof(permissions[0])
    };
}

// Send a message to the command sender.
static void send_message(pumpkin_plugin_command_borrow_command_sender_t sender,
                         const char *message) {
    plugin_string_t text;
    plugin_string_set(&text, message);
    pumpkin_plugin_text_own_text_component_t component =
        pumpkin_plugin_text_static_text_component_text(&text);
    pumpkin_plugin_command_method_command_sender_send_message(sender, component);
}

// Send a message directly to a player.
static void send_message_player(pumpkin_plugin_player_borrow_player_t player,
                                const char *message) {
    plugin_string_t text;
    plugin_string_set(&text, message);
    pumpkin_plugin_text_own_text_component_t component =
        pumpkin_plugin_text_static_text_component_text(&text);
    pumpkin_plugin_player_method_player_send_system_message(player, component, true);
}

// Apply freeze/unfreeze to every player matched by the command's player target.
static bool handle_player_targets(plugin_own_command_sender_t sender,
                                  plugin_own_consumed_args_t args,
                                  int32_t *ret,
                                  plugin_command_error_t *err,
                                  bool lock_movement) {
    (void)err;
    pumpkin_plugin_command_borrow_command_sender_t borrowed_sender =
        pumpkin_plugin_command_borrow_command_sender(sender);
    pumpkin_plugin_command_borrow_consumed_args_t borrowed_args =
        pumpkin_plugin_command_borrow_consumed_args(args);

    plugin_string_t key;
    plugin_string_set(&key, "player");
    pumpkin_plugin_command_arg_t arg = {0};
    pumpkin_plugin_command_method_consumed_args_get_value(
        borrowed_args, &key, &arg);
    plugin_string_free(&key);

    pumpkin_plugin_command_list_own_player_t players = arg.val.players;
    if (players.len == 0) {
        send_message(borrowed_sender, "No players matched that target.");
        *ret = 0;
        return true;
    }

    size_t affected_count = 0;
    for (size_t i = 0; i < players.len; i++) {
        pumpkin_plugin_player_borrow_player_t player =
            pumpkin_plugin_player_borrow_player(players.ptr[i]);
        pumpkin_plugin_player_uuid_t player_id;
        pumpkin_plugin_player_method_player_get_id(player, &player_id);
        if (!set_player_frozen(player_id, lock_movement)) {
            send_message(borrowed_sender,
                         "Could not save the frozen player; not freezing them.");
            continue;
        }
        pumpkin_plugin_player_method_player_set_movement_locked(
            player, lock_movement);
        send_message_player(player, lock_movement
                                        ? "You have been frozen."
                                        : "You have been unfrozen.");
        affected_count++;
    }
    pumpkin_plugin_command_list_own_player_free(&players);

    char summary[96];
    snprintf(summary, sizeof(summary), "%s %zu player(s).",
             lock_movement ? "Froze" : "Unfroze", affected_count);
    send_message(borrowed_sender, summary);
    *ret = 1;
    return true;
}

static bool handle_command_freeze(plugin_own_command_sender_t sender,
                           plugin_own_server_instance_t server,
                           plugin_own_consumed_args_t args,
                           int32_t *ret,
                           plugin_command_error_t *err) {
    (void)server;
    return handle_player_targets(sender, args, ret, err, true);
}

static bool handle_command_unfreeze(plugin_own_command_sender_t sender,
                           plugin_own_server_instance_t server,
                           plugin_own_consumed_args_t args,
                           int32_t *ret,
                           plugin_command_error_t *err) {
    (void)server;
    return handle_player_targets(sender, args, ret, err, false);
}

// Register a command that takes a player target.
static void register_command_noaliases(const char *name,
                                       const char *description,
                                       const char *required_permission_arg,
                                       uint32_t handler_id,
                                       plugin_own_context_t ctx) {

    plugin_string_t required_permission;
    plugin_string_set(&required_permission, required_permission_arg);

    plugin_string_t command_name;
    plugin_string_set(&command_name, name);
    plugin_string_t command_names_storage[] = { command_name };
    plugin_list_string_t command_names = {
        .ptr = command_names_storage,
        .len = sizeof(command_names_storage) / sizeof(command_names_storage[0])
    };

    plugin_string_t command_description;
    plugin_string_set(&command_description, description);
    pumpkin_plugin_command_own_command_t command =
        pumpkin_plugin_command_constructor_command(
            &command_names, &command_description);

    plugin_string_t argument_name;
    plugin_string_set(&argument_name, "player");
    pumpkin_plugin_command_argument_type_t argument_type = {0};
    argument_type.tag = PUMPKIN_PLUGIN_COMMAND_ARGUMENT_TYPE_PLAYERS;
    pumpkin_plugin_command_own_command_node_t argument_node =
        pumpkin_plugin_command_static_command_node_argument(
            &argument_name, &argument_type);
    pumpkin_plugin_command_method_command_node_execute_with_handler_id(
        pumpkin_plugin_command_borrow_command_node(argument_node),
        handler_id);
    pumpkin_plugin_command_method_command_then(
        pumpkin_plugin_command_borrow_command(command), argument_node);

    pumpkin_plugin_context_method_context_register_command(
        pumpkin_plugin_context_borrow_context(ctx), command, &required_permission);
}
// Entry function
// Registers the permission node and commands
static void on_load(plugin_own_context_t ctx) {
    pumpkin_plugin_permission_permission_t permission = {0};
    plugin_string_set(&permission.node, "slashfreeze:freeze");
    plugin_string_set(&permission.description,
                      "Allows executing the /freeze and /unfreeze commands");
    permission.default_.tag =
        PUMPKIN_PLUGIN_PERMISSION_PERMISSION_DEFAULT_OP;

    plugin_string_t error = {0};
    bool permission_registered =
        pumpkin_plugin_context_method_context_register_permission(
            pumpkin_plugin_context_borrow_context(ctx), &permission, &error);
    if (!permission_registered) {
        pumpkin_plugin_logging_log(PUMPKIN_PLUGIN_LOGGING_LEVEL_ERROR, &error);
        plugin_string_free(&error);
        return;
    }
    pumpkin_plugin_context_method_context_register_event(
        pumpkin_plugin_context_borrow_context(ctx),
        (uint32_t)(uintptr_t)handle_player_join,
        PUMPKIN_PLUGIN_EVENT_EVENT_TYPE_PLAYER_JOIN_EVENT,
        PUMPKIN_PLUGIN_EVENT_EVENT_PRIORITY_NORMAL,
        true);

    register_command_noaliases("freeze",
        "Freeze target players. Usage: /freeze <player>",
        "slashfreeze:freeze",
        (uint32_t)(uintptr_t)handle_command_freeze,
        ctx);
    register_command_noaliases("unfreeze",
        "UnFreezes the player",
        "slashfreeze:freeze",
        (uint32_t)(uintptr_t)handle_command_unfreeze,
        ctx);
}

REGISTER_PUMPKIN_PLUGIN(((pumpkin_plugin_t){
    .get_metadata = get_meta,
    .on_load = on_load,
    .on_unload = on_unload
}))
