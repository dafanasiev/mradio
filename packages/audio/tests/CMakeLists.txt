add_executable(mradio_audio_tests test_mpv_engine.cpp)

target_link_libraries(mradio_audio_tests
    PRIVATE mradio::audio Catch2::Catch2WithMain)

mradio_configure_target(mradio_audio_tests)

include(Catch)
# These tests drive a real mpv instance against a generated WAV file. They need
# no network and no sound card (the engine is built with --ao=null), but they
# do wait on real playback, so they get a longer timeout than a pure unit test.
catch_discover_tests(mradio_audio_tests PROPERTIES TIMEOUT 60)
