// SPDX-License-Identifier: MIT
#include "blemgr/glib_scheduler.hpp"

#include <glib.h>

#include <algorithm>
#include <thread>

namespace blemgr {

struct GlibScheduler::Impl {
    GMainContext* ctx = g_main_context_new();
    GMainLoop* loop = g_main_loop_new(ctx, FALSE);
    std::thread thread;
    std::thread::id loopId;

    ~Impl() {
        g_main_loop_unref(loop);
        g_main_context_unref(ctx);
    }

    static gboolean run(gpointer p) {
        (*static_cast<std::function<void()>*>(p))();
        return G_SOURCE_REMOVE;
    }
    static void destroy(gpointer p) { delete static_cast<std::function<void()>*>(p); }

    guint attach(GSource* src, std::function<void()> fn) {
        g_source_set_callback(src, &Impl::run, new std::function<void()>(std::move(fn)),
                              &Impl::destroy);
        guint id = g_source_attach(src, ctx);
        g_source_unref(src);
        return id;
    }
};

GlibScheduler::GlibScheduler() : d_(std::make_unique<Impl>()) {}

GlibScheduler::~GlibScheduler() { stop(); }

void GlibScheduler::start() {
    if (d_->thread.joinable()) return;
    d_->thread = std::thread([this] {
        d_->loopId = std::this_thread::get_id();
        g_main_context_push_thread_default(d_->ctx);
        g_main_loop_run(d_->loop);
        // Drain whatever is still dispatchable so GDBus can release its references.
        while (g_main_context_iteration(d_->ctx, FALSE)) {
        }
        g_main_context_pop_thread_default(d_->ctx);
    });
}

void GlibScheduler::stop() {
    if (!d_->thread.joinable()) return;
    // Quit from inside the loop so it is ordered after previously posted work.
    post([this] { g_main_loop_quit(d_->loop); });
    d_->thread.join();
}

void GlibScheduler::post(std::function<void()> fn) {
    GSource* src = g_idle_source_new();
    g_source_set_priority(src, G_PRIORITY_DEFAULT);
    d_->attach(src, std::move(fn));
}

IScheduler::TimerId GlibScheduler::postDelayed(std::chrono::milliseconds delay,
                                               std::function<void()> fn) {
    GSource* src = g_timeout_source_new(static_cast<guint>(std::max<int64_t>(0, delay.count())));
    return d_->attach(src, std::move(fn));
}

void GlibScheduler::cancel(TimerId id) {
    if (!id) return;
    if (GSource* s = g_main_context_find_source_by_id(d_->ctx, static_cast<guint>(id)))
        g_source_destroy(s);
}

bool GlibScheduler::inLoopThread() const { return std::this_thread::get_id() == d_->loopId; }

}  // namespace blemgr
