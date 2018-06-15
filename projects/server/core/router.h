/**
 * Place to decide should the client be allowed to connect
 */
void on_connection_request(librg_event_t *event) {
    if (mod.settings.password.size() == 0) {
        return;
    }

    // read password
    u32 size = librg_data_ru32(event->data);
    std::string password = "";
    for (usize i = 0; i < size; ++i) {
        password += librg_data_ru8(event->data);
    }

    // if not matches - reject
    if (password != mod.settings.password) {
        librg_event_reject(event);
    }
}

/**
 * On client connected
 */
void on_connect_accepted(librg_event_t *event) {
    auto entity = event->entity;
    // entity->position = vec3(-421.75f, 479.31f, 0.05f);

    mod_log("spawning player %u at: %f %f %f\n",
        entity->id,
        entity->position.x,
        entity->position.y,
        entity->position.z
    );

    entity->user_data = new ped_t();
    librg_entity_control_set(event->ctx, event->entity->id, event->entity->client_peer);

    m2o_args args = {0};
    m2o_args_init(&args);
    m2o_args_push_integer(&args, event->entity->id);
    m2o_event_trigger("player_connect", &args);
    m2o_args_free(&args);
}

void on_connect_disconnect(librg_event_t *event) {
    auto ped = get_ped(event->entity);

    m2o_args args = {0};
    m2o_args_init(&args);
    m2o_args_push_integer(&args, event->entity->id);
    m2o_event_trigger("player_disconnect", &args);
    m2o_args_free(&args);

    librg_entity_iteratex(event->ctx, LIBRG_ENTITY_ALIVE, entityid, {
        auto entity = librg_entity_fetch(ctx, entityid);
        if (entity->type != TYPE_CAR) continue;
        if (!(entity->flags & LIBRG_ENTITY_CONTROLLED)) continue;

        if (librg_entity_control_get(event->ctx, entity->id) == event->entity->client_peer) {
            mod_log("[info] removing control on %d for disconnected client\n", entity->id);
            librg_entity_control_remove(event->ctx, entity->id); // remove control from our ped
        }
    });

    delete (char *)event->entity->user_data;
}

void on_entity_create(librg_event_t *event) {
    mod_log("[info] sending a create packet for entity: %d\n", event->entity->id);

    switch (event->entity->type) {
        case TYPE_PED: { on_ped_create(event); } break;
        case TYPE_CAR: { on_car_create(event); } break;
    }
}

void on_entity_update(librg_event_t *event) {
    switch (event->entity->type) {
        case TYPE_PED: { auto ped = get_ped(event->entity); librg_data_wptr(event->data, &ped->stream, sizeof(ped->stream)); } break;
        case TYPE_CAR: { auto car = get_car(event->entity); librg_data_wptr(event->data, &car->stream, sizeof(car->stream)); } break;
    }
}

void on_entity_client_update(librg_event_t *event) {
    switch (event->entity->type) {
        case TYPE_PED: { auto ped = get_ped(event->entity); librg_data_rptr(event->data, &ped->stream, sizeof(ped->stream)); } break;
        case TYPE_CAR: { auto car = get_car(event->entity); librg_data_rptr(event->data, &car->stream, sizeof(car->stream)); } break;
    }
}

void on_entity_remove(librg_event_t *event) {
    if (!event->entity) return; // entity has been deleted
    mod_log("[info] sending a remove packet for entity: %d\n", event->entity->id);

    if (librg_entity_control_get(event->ctx, event->entity->id) == event->peer) {
        mod_log("removing control of entity: %d for peer: %llx\n", event->entity->id, (u64)event->peer);
        librg_entity_control_remove(event->ctx, event->entity->id);
    }

    switch (event->entity->type) {
        case TYPE_PED: { on_ped_remove(event); } break;
        case TYPE_CAR: { on_car_remove(event); } break;
    }
}

void on_user_name_set(librg_message_t *msg) {
    // TODO: read in a temp var first, and then apply to struct
    u8 strsize = librg_data_ru8(msg->data);
    auto entity = librg_entity_find(msg->ctx, msg->peer);
    auto ped    = get_ped(entity);
    librg_data_rptr(msg->data, ped->name, strsize);

    mod_log("[info] client %d requested name change to: %s\n", entity->id, ped->name);

    mod_message_send_instream_except(ctx, MOD_USER_SET_NAME, entity->id, msg->peer, [&](librg_data_t *data) {
        librg_data_wu32(data, entity->id);
        librg_data_wu8(data, strsize);
        librg_data_wptr(data, (void *)ped->name, strsize);
    });
}

void on_user_message(librg_message_t *msg) {
    char message_buffer[632], input_buffer[512];

    u32 strsize = librg_data_ru32(msg->data);
    librg_data_rptr(msg->data, input_buffer, strsize);
    input_buffer[strsize] = '\0';

    for (int i = 0; i < strsize; i++) input_buffer[i] = input_buffer[i] == '%' ? '\045' : input_buffer[i];

    auto entity = librg_entity_find(msg->ctx, msg->peer);
    auto ped = get_ped(entity);

    zpl_snprintf(message_buffer, 632, "%s (%d): %s", ped->name, entity->id, input_buffer);

    mod_log("[chat] %s \n", message_buffer);

    mod_message_send(ctx, MOD_USER_MESSAGE, [&](librg_data_t *data) {
        librg_data_wu32(data, zpl_strlen(message_buffer));
        librg_data_wptr(data, message_buffer, zpl_strlen(message_buffer));
    });
}

void mod_register_routes(librg_ctx_t *ctx) {
    librg_event_add(ctx, LIBRG_CONNECTION_REQUEST,      on_connection_request);
    librg_event_add(ctx, LIBRG_CONNECTION_ACCEPT,       on_connect_accepted);
    librg_event_add(ctx, LIBRG_CONNECTION_DISCONNECT,   on_connect_disconnect);

    librg_event_add(ctx, LIBRG_ENTITY_CREATE,           on_entity_create);
    librg_event_add(ctx, LIBRG_ENTITY_UPDATE,           on_entity_update);
    librg_event_add(ctx, LIBRG_ENTITY_REMOVE,           on_entity_remove);
    librg_event_add(ctx, LIBRG_CLIENT_STREAMER_UPDATE,  on_entity_client_update);

    librg_network_add(ctx, MOD_PED_CREATE,              on_ped_create_command); /* testing */
    librg_network_add(ctx, MOD_CAR_CREATE,              on_car_create_command); /* testing */
    librg_network_add(ctx, MOD_CAR_ENTER_START,         on_car_enter_start);
    librg_network_add(ctx, MOD_CAR_ENTER_FINISH,        on_car_enter_finish);
    librg_network_add(ctx, MOD_CAR_EXIT_START,          on_car_exit_start);
    librg_network_add(ctx, MOD_CAR_EXIT_FINISH,         on_car_exit_finish);

    librg_network_add(ctx, MOD_USER_SET_NAME,           on_user_name_set);
    librg_network_add(ctx, MOD_USER_MESSAGE,            on_user_message);
}