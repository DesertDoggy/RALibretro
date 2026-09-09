#include "rahasher_stream.h"

#include "../src/Util.h"

#include <rcheevos/include/rc_hash.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <stdlib.h>
#include <unistd.h>

#ifdef HAVE_CHD
void rc_hash_init_chd_cdreader(); /* in HashCHD.cpp */
#endif

void initHash3DS(const std::string& systemDir); /* in Hash3DS.cpp */

struct rahasher_stream_ctx
{
  uint32_t console_id = 0;
  uint64_t expected_total_bytes = 0;
  uint64_t bytes_fed = 0;
  uint64_t bytes_hashed = 0;
  uint64_t next_feed_progress_mark = 0;
  bool finalized = false;

  std::string source_name;
  std::string source_extension;
  std::string system_dir;
  std::string temp_dir;
  std::string temp_path;
  std::string last_error;
  char hash[33] = {};

  FILE* stream_fp = nullptr;

  rahasher_progress_callback progress_cb = nullptr;
  rahasher_notice_callback notice_cb = nullptr;
  void* userdata = nullptr;
};

namespace
{
  static const int RC_CONSOLE_MAX = 90;

  static std::mutex g_hash_mutex;
  static thread_local rahasher_stream_ctx* g_active_ctx = nullptr;

  static void set_error(rahasher_stream_ctx* ctx, const char* message)
  {
    if (!ctx)
      return;

    ctx->last_error = message ? message : "unknown error";
    if (ctx->notice_cb)
      ctx->notice_cb(RAHASHER_NOTICE_ERROR, ctx->last_error.c_str(), ctx->userdata);
  }

  static std::string to_lower(std::string value)
  {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return value;
  }

  static std::string get_basename(const std::string& path)
  {
    const size_t slash_pos = path.find_last_of("/\\");
    if (slash_pos == std::string::npos)
      return path.empty() ? std::string("stream.bin") : path;

    return path.substr(slash_pos + 1);
  }

  static std::string get_extension(const std::string& path)
  {
    const std::string base = get_basename(path);
    const size_t dot_pos = base.find_last_of('.');
    if (dot_pos == std::string::npos)
      return std::string();

    return to_lower(base.substr(dot_pos));
  }

  static void emit_progress(rahasher_stream_ctx* ctx, const char* stage, uint64_t bytes_read, uint64_t bytes_expected)
  {
    if (ctx && ctx->progress_cb)
      ctx->progress_cb(stage, bytes_read, bytes_expected, ctx->userdata);
  }

  static void emit_notice(rahasher_stream_ctx* ctx, int severity, const char* message)
  {
    if (ctx && ctx->notice_cb)
      ctx->notice_cb(severity, message, ctx->userdata);
  }

  static void RC_CCONV iterator_verbose_bridge(const char* message, const rc_hash_iterator_t* iterator)
  {
    rahasher_stream_ctx* ctx = iterator ? static_cast<rahasher_stream_ctx*>(iterator->userdata) : nullptr;
    emit_notice(ctx, RAHASHER_NOTICE_VERBOSE, message ? message : "");
  }

  static void RC_CCONV iterator_error_bridge(const char* message, const rc_hash_iterator_t* iterator)
  {
    rahasher_stream_ctx* ctx = iterator ? static_cast<rahasher_stream_ctx*>(iterator->userdata) : nullptr;
    emit_notice(ctx, RAHASHER_NOTICE_ERROR, message ? message : "");
  }

  static void* RC_CCONV filereader_open(const char* path_utf8)
  {
    return util::openFile(nullptr, path_utf8 ? path_utf8 : "", "rb");
  }

  static void RC_CCONV filereader_seek(void* file_handle, int64_t offset, int origin)
  {
#if defined(_WIN32)
    _fseeki64(static_cast<FILE*>(file_handle), offset, origin);
#else
    fseeko(static_cast<FILE*>(file_handle), static_cast<off_t>(offset), origin);
#endif
  }

  static int64_t RC_CCONV filereader_tell(void* file_handle)
  {
#if defined(_WIN32)
    return _ftelli64(static_cast<FILE*>(file_handle));
#else
    return static_cast<int64_t>(ftello(static_cast<FILE*>(file_handle)));
#endif
  }

  static size_t RC_CCONV filereader_read(void* file_handle, void* buffer, size_t requested_bytes)
  {
    size_t read_bytes = fread(buffer, 1, requested_bytes, static_cast<FILE*>(file_handle));

    rahasher_stream_ctx* ctx = g_active_ctx;
    if (ctx)
    {
      ctx->bytes_hashed += static_cast<uint64_t>(read_bytes);
      const uint64_t expected = ctx->expected_total_bytes ? ctx->expected_total_bytes : ctx->bytes_fed;
      emit_progress(ctx, "hash-read", ctx->bytes_hashed, expected);
    }

    return read_bytes;
  }

  static void RC_CCONV filereader_close(void* file_handle)
  {
    fclose(static_cast<FILE*>(file_handle));
  }

  static int run_hash(rahasher_stream_ctx* ctx)
  {
    if (ctx->console_id == RC_CONSOLE_NINTENDO_3DS)
      initHash3DS(ctx->system_dir);

    rc_hash_iterator_t iterator;
    std::memset(&iterator, 0, sizeof(iterator));
    rc_hash_initialize_iterator(&iterator, ctx->temp_path.c_str(), nullptr, 0);

    iterator.userdata = ctx;
    iterator.callbacks.verbose_message = iterator_verbose_bridge;
    iterator.callbacks.error_message = iterator_error_bridge;
    iterator.callbacks.filereader.open = filereader_open;
    iterator.callbacks.filereader.seek = filereader_seek;
    iterator.callbacks.filereader.tell = filereader_tell;
    iterator.callbacks.filereader.read = filereader_read;
    iterator.callbacks.filereader.close = filereader_close;

#ifdef HAVE_CHD
    if (ctx->source_extension == ".chd")
      rc_hash_init_chd_cdreader();
    else
#endif
      rc_hash_init_default_cdreader();

    std::lock_guard<std::mutex> lock(g_hash_mutex);
    g_active_ctx = ctx;

    const int ok = rc_hash_generate(ctx->hash, ctx->console_id, &iterator);

    g_active_ctx = nullptr;
    rc_hash_destroy_iterator(&iterator);

    return ok;
  }
}

void rhash_log_error_message(const char* message)
{
  if (g_active_ctx && g_active_ctx->notice_cb)
    g_active_ctx->notice_cb(RAHASHER_NOTICE_ERROR, message ? message : "", g_active_ctx->userdata);
  else
    std::fprintf(stderr, "%s\n", message ? message : "");
}

rahasher_stream_ctx_t* rahasher_stream_begin(
  uint32_t console_id,
  const char* source_name,
  const char* system_dir,
  uint64_t expected_total_bytes,
  rahasher_progress_callback progress_cb,
  rahasher_notice_callback notice_cb,
  void* userdata)
{
  if (!source_name || console_id == 0)
    return nullptr;

  std::unique_ptr<rahasher_stream_ctx> ctx(new rahasher_stream_ctx());
  ctx->console_id = console_id;
  ctx->expected_total_bytes = expected_total_bytes;
  ctx->source_name = source_name;
  ctx->source_extension = get_extension(ctx->source_name);
  ctx->system_dir = system_dir ? system_dir : ".";
  ctx->progress_cb = progress_cb;
  ctx->notice_cb = notice_cb;
  ctx->userdata = userdata;

  if (ctx->console_id > RC_CONSOLE_MAX)
  {
    set_error(ctx.get(), "console_id must be a specific console id (<= 90)");
    return nullptr;
  }

  char temp_dir_template[] = "/tmp/rahasher_stream_XXXXXX";
  char* temp_dir = mkdtemp(temp_dir_template);
  if (!temp_dir)
  {
    set_error(ctx.get(), "failed to create temporary directory");
    return nullptr;
  }

  ctx->temp_dir = temp_dir;

  std::string base = get_basename(ctx->source_name);
  if (base.empty())
    base = "stream.bin";

  ctx->temp_path = ctx->temp_dir + "/" + base;
  ctx->stream_fp = util::openFile(nullptr, ctx->temp_path, "wb");
  if (!ctx->stream_fp)
  {
    set_error(ctx.get(), "failed to open temporary file for stream input");
    return nullptr;
  }

  emit_progress(ctx.get(), "feed-start", 0, ctx->expected_total_bytes);
  return ctx.release();
}

int rahasher_stream_feed(rahasher_stream_ctx_t* ctx, const uint8_t* data, size_t size)
{
  if (!ctx || !ctx->stream_fp || !data || size == 0)
    return RAHASHER_STREAM_ERR_INVALID_ARG;

  if (ctx->finalized)
  {
    set_error(ctx, "feed called after finish");
    return RAHASHER_STREAM_ERR_INVALID_ARG;
  }

  const size_t written = fwrite(data, 1, size, ctx->stream_fp);
  if (written != size)
  {
    set_error(ctx, "failed to write chunk into temporary file");
    return RAHASHER_STREAM_ERR_IO;
  }

  ctx->bytes_fed += static_cast<uint64_t>(written);

  /* Reduce callback noise while preserving monotonic progress visibility. */
  const uint64_t kStep = 1024 * 1024;
  if (ctx->bytes_fed >= ctx->next_feed_progress_mark)
  {
    const uint64_t expected = ctx->expected_total_bytes;
    emit_progress(ctx, "feed", ctx->bytes_fed, expected);
    ctx->next_feed_progress_mark = ctx->bytes_fed + kStep;
  }

  return RAHASHER_STREAM_OK;
}

int rahasher_stream_finish(rahasher_stream_ctx_t* ctx, char out_hash[33])
{
  if (!ctx || !out_hash)
    return RAHASHER_STREAM_ERR_INVALID_ARG;

  if (ctx->finalized)
  {
    std::memcpy(out_hash, ctx->hash, 33);
    return RAHASHER_STREAM_OK;
  }

  if (!ctx->stream_fp)
  {
    set_error(ctx, "stream file handle is not valid");
    return RAHASHER_STREAM_ERR_IO;
  }

  if (ctx->source_extension == ".m3u")
  {
    set_error(ctx, "m3u stream input is not supported; provide referenced disc file stream instead");
    return RAHASHER_STREAM_ERR_UNSUPPORTED;
  }

  emit_progress(ctx, "feed-complete", ctx->bytes_fed, ctx->expected_total_bytes);

  if (fflush(ctx->stream_fp) != 0)
  {
    set_error(ctx, "failed to flush temporary file");
    return RAHASHER_STREAM_ERR_IO;
  }

  fclose(ctx->stream_fp);
  ctx->stream_fp = nullptr;

  ctx->bytes_hashed = 0;
  if (!run_hash(ctx))
  {
    if (ctx->last_error.empty())
      set_error(ctx, "hash generation failed");

    return RAHASHER_STREAM_ERR_HASH;
  }

  ctx->finalized = true;
  std::memcpy(out_hash, ctx->hash, 33);
  emit_progress(ctx, "done", ctx->bytes_hashed, ctx->expected_total_bytes ? ctx->expected_total_bytes : ctx->bytes_fed);
  return RAHASHER_STREAM_OK;
}

const char* rahasher_stream_get_last_error(const rahasher_stream_ctx_t* ctx)
{
  if (!ctx)
    return "invalid context";

  return ctx->last_error.c_str();
}

const char* rahasher_stream_get_hash(const rahasher_stream_ctx_t* ctx)
{
  if (!ctx || !ctx->finalized)
    return nullptr;

  return ctx->hash;
}

uint64_t rahasher_stream_get_bytes_fed(const rahasher_stream_ctx_t* ctx)
{
  return ctx ? ctx->bytes_fed : 0;
}

void rahasher_stream_destroy(rahasher_stream_ctx_t* ctx)
{
  if (!ctx)
    return;

  if (ctx->stream_fp)
    fclose(ctx->stream_fp);

  if (!ctx->temp_path.empty())
    remove(ctx->temp_path.c_str());

  if (!ctx->temp_dir.empty())
    rmdir(ctx->temp_dir.c_str());

  delete ctx;
}
