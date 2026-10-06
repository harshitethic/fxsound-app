/*
 * FxSound Linux - PipeWire sink probe
 * AGPL-3.0-or-later, matching the upstream FxSound project.
 */
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/utils/result.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>

struct App {
    pw_main_loop* loop{};
    pw_stream* stream{};
    std::atomic<unsigned long long> frames{0};
};

static void on_state_changed(void* userdata,
                             enum pw_stream_state old_state,
                             enum pw_stream_state state,
                             const char* error)
{
    auto* app = static_cast<App*>(userdata);
    std::fprintf(stderr, "FxSound Linux sink: %s -> %s",
                 pw_stream_state_as_string(old_state),
                 pw_stream_state_as_string(state));
    if (error) std::fprintf(stderr, " (%s)", error);
    std::fprintf(stderr, "\n");
    if (state == PW_STREAM_STATE_STREAMING || state == PW_STREAM_STATE_PAUSED) {
        std::fprintf(stderr, "node-id=%u\n", pw_stream_get_node_id(app->stream));
    }
}

static void on_process(void* userdata)
{
    auto* app = static_cast<App*>(userdata);
    pw_buffer* b = pw_stream_dequeue_buffer(app->stream);
    if (!b) return;

    spa_buffer* buf = b->buffer;
    if (buf && buf->n_datas > 0 && buf->datas[0].chunk) {
        const uint32_t bytes = buf->datas[0].chunk->size;
        app->frames.fetch_add(bytes / (sizeof(float) * 2), std::memory_order_relaxed);
    }
    pw_stream_queue_buffer(app->stream, b);
}

static const pw_stream_events kEvents = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_state_changed,
    .process = on_process,
};

static App* g_app = nullptr;
static void handle_signal(int)
{
    if (g_app && g_app->loop) pw_main_loop_quit(g_app->loop);
}

int main(int argc, char** argv)
{
    pw_init(&argc, &argv);

    App app;
    g_app = &app;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    app.loop = pw_main_loop_new(nullptr);
    if (!app.loop) {
        std::fprintf(stderr, "failed to create PipeWire main loop\n");
        return 1;
    }

    auto* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_ROLE, "DSP",
        PW_KEY_MEDIA_CLASS, "Audio/Sink",
        PW_KEY_NODE_NAME, "fxsound_linux_test_sink",
        PW_KEY_NODE_DESCRIPTION, "FxSound Linux Test Sink",
        PW_KEY_NODE_NICK, "FxSound Linux Test",
        PW_KEY_NODE_VIRTUAL, "true",
        PW_KEY_NODE_AUTOCONNECT, "false",
        "audio.channels", "2",
        "audio.position", "[ FL FR ]",
        nullptr);

    app.stream = pw_stream_new_simple(
        pw_main_loop_get_loop(app.loop),
        "FxSound Linux Test Sink",
        props,
        &kEvents,
        &app);

    if (!app.stream) {
        std::fprintf(stderr, "failed to create PipeWire stream\n");
        pw_main_loop_destroy(app.loop);
        pw_deinit();
        return 2;
    }

    uint8_t pod_buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(pod_buffer, sizeof(pod_buffer));
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = 48000;
    info.channels = 2;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;

    const spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);

    const int rc = pw_stream_connect(
        app.stream,
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_MAP_BUFFERS),
        params,
        1);

    if (rc < 0) {
        std::fprintf(stderr, "pw_stream_connect failed: %s\n", spa_strerror(rc));
        pw_stream_destroy(app.stream);
        pw_main_loop_destroy(app.loop);
        pw_deinit();
        return 3;
    }

    std::fprintf(stderr, "FxSound Linux test sink created; Ctrl+C to stop.\n");
    pw_main_loop_run(app.loop);

    std::fprintf(stderr, "captured-frames=%llu\n",
                 app.frames.load(std::memory_order_relaxed));

    pw_stream_destroy(app.stream);
    pw_main_loop_destroy(app.loop);
    pw_deinit();
    return 0;
}
