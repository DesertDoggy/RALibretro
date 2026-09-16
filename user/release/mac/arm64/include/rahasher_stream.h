#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAHASHER_NOTICE_VERBOSE 0
#define RAHASHER_NOTICE_ERROR 1

#define RAHASHER_STREAM_OK 0
#define RAHASHER_STREAM_ERR_INVALID_ARG -1
#define RAHASHER_STREAM_ERR_IO -2
#define RAHASHER_STREAM_ERR_HASH -3
#define RAHASHER_STREAM_ERR_UNSUPPORTED -4

typedef struct rahasher_stream_ctx rahasher_stream_ctx_t;

typedef void (*rahasher_progress_callback)(
  const char* stage,
  uint64_t bytes_read,
  uint64_t bytes_expected,
  void* userdata);

typedef void (*rahasher_notice_callback)(
  int severity,
  const char* message,
  void* userdata);

/*
 * source_name is used for extension-sensitive hash logic and may be just a filename.
 * expected_total_bytes can be 0 if unknown.
 */
rahasher_stream_ctx_t* rahasher_stream_begin(
  uint32_t console_id,
  const char* source_name,
  const char* system_dir,
  uint64_t expected_total_bytes,
  rahasher_progress_callback progress_cb,
  rahasher_notice_callback notice_cb,
  void* userdata);

int rahasher_stream_feed(
  rahasher_stream_ctx_t* ctx,
  const uint8_t* data,
  size_t size);

/* out_hash must point to a 33-byte buffer */
int rahasher_stream_finish(
  rahasher_stream_ctx_t* ctx,
  char out_hash[33]);

const char* rahasher_stream_get_last_error(const rahasher_stream_ctx_t* ctx);
const char* rahasher_stream_get_hash(const rahasher_stream_ctx_t* ctx);
uint64_t rahasher_stream_get_bytes_fed(const rahasher_stream_ctx_t* ctx);
void rahasher_stream_destroy(rahasher_stream_ctx_t* ctx);

#ifdef __cplusplus
}
#endif
