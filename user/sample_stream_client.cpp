#include "rahasher_stream.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void on_progress(const char* stage, uint64_t read, uint64_t expected, void* ud)
{
  (void)ud;
  if (expected > 0)
    std::fprintf(stderr, "[progress] %s: %llu/%llu\n", stage, (unsigned long long)read, (unsigned long long)expected);
  else
    std::fprintf(stderr, "[progress] %s: %llu\n", stage, (unsigned long long)read);
}

static void on_notice(int severity, const char* message, void* ud)
{
  (void)ud;
  const char* level = (severity == RAHASHER_NOTICE_ERROR) ? "error" : "verbose";
  std::fprintf(stderr, "[notice/%s] %s\n", level, message ? message : "");
}

int main(int argc, char** argv)
{
  if (argc < 3)
  {
    std::fprintf(stderr, "Usage: %s <console_id> <file_path> [system_dir]\n", argv[0]);
    return 1;
  }

  const uint32_t console_id = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
  const char* file_path = argv[2];
  const char* system_dir = (argc >= 4) ? argv[3] : ".";

  FILE* fp = std::fopen(file_path, "rb");
  if (!fp)
  {
    std::perror("fopen");
    return 1;
  }

  std::fseek(fp, 0, SEEK_END);
  const long file_size = std::ftell(fp);
  std::fseek(fp, 0, SEEK_SET);

  uint64_t expected = 0;
  if (file_size > 0)
    expected = static_cast<uint64_t>(file_size);

  rahasher_stream_ctx_t* ctx = rahasher_stream_begin(console_id, file_path, system_dir, expected, on_progress, on_notice, nullptr);
  if (!ctx)
  {
    std::fprintf(stderr, "rahasher_stream_begin failed\n");
    std::fclose(fp);
    return 1;
  }

  std::vector<uint8_t> buffer(64 * 1024);
  while (!std::feof(fp))
  {
    const size_t read_bytes = std::fread(buffer.data(), 1, buffer.size(), fp);
    if (read_bytes == 0)
      break;

    const int feed_result = rahasher_stream_feed(ctx, buffer.data(), read_bytes);
    if (feed_result != RAHASHER_STREAM_OK)
    {
      std::fprintf(stderr, "feed failed: %s\n", rahasher_stream_get_last_error(ctx));
      rahasher_stream_destroy(ctx);
      std::fclose(fp);
      return 1;
    }
  }

  std::fclose(fp);

  char hash[33] = {};
  const int finish_result = rahasher_stream_finish(ctx, hash);
  if (finish_result != RAHASHER_STREAM_OK)
  {
    std::fprintf(stderr, "finish failed: %s\n", rahasher_stream_get_last_error(ctx));
    rahasher_stream_destroy(ctx);
    return 1;
  }

  std::printf("%s\n", hash);
  rahasher_stream_destroy(ctx);
  return 0;
}
