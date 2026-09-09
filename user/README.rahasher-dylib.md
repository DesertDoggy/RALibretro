# RAHasher stream dylib

This user-local implementation exposes a chunk-feed API and builds a shared library without changing existing src/ code.

## Output layout
- user/release/{platform}/{arch}/dynamic/libRAHasher.{dylib|so|dll}
- user/release/{platform}/{arch}/iclude/rahasher_stream.h
- user/release/{platform}/{arch}/static/libRAHasher.a
- user/release/{platform}/{arch}/bin/rahasher_stream_sample
- intermediate objects: user/_build

## Build
- Auto target detect, dynamic by default:
  - ./user/build_rahasher.sh
- Explicit target/type/version:
  - ./user/build_rahasher.sh --target mac --type all --version v1
  - note: version is currently ignored in output path layout

## Supported target/arch mapping
- mac, iphone, android: arm64
- windows, linux: x64

## API summary
Header: user/rahasher_stream.h
- rahasher_stream_begin(console_id, source_name, system_dir, expected_total_bytes, progress_cb, notice_cb, userdata)
- rahasher_stream_feed(ctx, data, size)
- rahasher_stream_finish(ctx, out_hash)
- rahasher_stream_get_last_error(ctx)
- rahasher_stream_get_hash(ctx)
- rahasher_stream_get_bytes_fed(ctx)
- rahasher_stream_destroy(ctx)

## Progress and notices
- Progress callback emits:
  - stage="feed-start", "feed", "feed-complete", "hash-read", "done"
  - bytes_read and bytes_expected (0 when unknown)
- Notice callback forwards verbose/error messages from hashing pipeline.

## Behavior notes
- No done flag in API. finish returns hash or error.
- Caller controls abort by stopping feed or destroying context.
- .m3u stream input is not supported in this initial implementation.
- Console id must be specific (<= 90 in current rcheevos mapping path used here).

## Sample
- Source: user/sample_stream_client.cpp
- Build sample binary:
  - make -f user/Makefile.rahasher bin TARGET_PLATFORM=mac TARGET_ARCH=arm64 VERSION=dev

## Tests
- Tests are in user/tests.
- Run:
  - ./user/tests/run_tests.sh
