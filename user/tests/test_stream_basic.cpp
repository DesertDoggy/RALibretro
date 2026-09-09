#include "../rahasher_stream.h"

#include <rcheevos/include/rc_hash.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
  struct Stats
  {
    uint32_t progress_calls = 0;
    uint32_t notice_calls = 0;
  };

  static void on_progress(const char* stage, uint64_t bytes_read, uint64_t bytes_expected, void* userdata)
  {
    (void)stage;
    (void)bytes_read;
    (void)bytes_expected;

    Stats* stats = static_cast<Stats*>(userdata);
    stats->progress_calls++;
  }

  static void on_notice(int severity, const char* message, void* userdata)
  {
    (void)severity;
    (void)message;

    Stats* stats = static_cast<Stats*>(userdata);
    stats->notice_calls++;
  }

  static bool write_file(const std::string& path, const std::vector<uint8_t>& data)
  {
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp)
      return false;

    const size_t written = std::fwrite(data.data(), 1, data.size(), fp);
    std::fclose(fp);
    return written == data.size();
  }

  static bool run_chunk_case(const std::vector<uint8_t>& data, size_t chunk_size, const char* expected_hash)
  {
    Stats stats;
    rahasher_stream_ctx_t* ctx = rahasher_stream_begin(
      RC_CONSOLE_GAMEBOY_ADVANCE,
      "test.gba",
      ".",
      static_cast<uint64_t>(data.size()),
      on_progress,
      on_notice,
      &stats);

    if (!ctx)
      return false;

    for (size_t offset = 0; offset < data.size(); offset += chunk_size)
    {
      const size_t remaining = data.size() - offset;
      const size_t len = remaining < chunk_size ? remaining : chunk_size;
      if (rahasher_stream_feed(ctx, &data[offset], len) != RAHASHER_STREAM_OK)
      {
        rahasher_stream_destroy(ctx);
        return false;
      }
    }

    char actual_hash[33] = {};
    const int rc = rahasher_stream_finish(ctx, actual_hash);
    const bool ok = (rc == RAHASHER_STREAM_OK) && (std::strcmp(actual_hash, expected_hash) == 0) && (stats.progress_calls > 0);
    rahasher_stream_destroy(ctx);
    return ok;
  }
}

int main()
{
  const std::string temp_path = "/tmp/rahasher_stream_test.gba";

  std::vector<uint8_t> data;
  data.resize(1024 * 1024);
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = static_cast<uint8_t>((i * 131u) & 0xFFu);

  if (!write_file(temp_path, data))
  {
    std::fprintf(stderr, "failed to write temp file\n");
    return 1;
  }

  char expected_hash[33] = {};
  if (!rc_hash_generate_from_file(expected_hash, RC_CONSOLE_GAMEBOY_ADVANCE, temp_path.c_str()))
  {
    std::fprintf(stderr, "reference hash generation failed\n");
    std::remove(temp_path.c_str());
    return 1;
  }

  const bool case_4k = run_chunk_case(data, 4 * 1024, expected_hash);
  const bool case_64k = run_chunk_case(data, 64 * 1024, expected_hash);
  const bool case_1m = run_chunk_case(data, 1024 * 1024, expected_hash);

  std::remove(temp_path.c_str());

  if (!case_4k || !case_64k || !case_1m)
  {
    std::fprintf(stderr, "stream hash test failed\n");
    return 1;
  }

  std::printf("stream hash test passed\n");
  return 0;
}
