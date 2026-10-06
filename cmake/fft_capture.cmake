# The diagnostic and configured workers share the repaired capture/DSP path.
function(tm_add_fft_capture)
    set(original "${TM_TIC80_SOURCE}/src/ext/fft.c")
    file(SHA256 "${original}" original_sha)
    if(NOT original_sha STREQUAL "f00b25684b06f67300b90bb79fe8bb4f6de23b4d19c5e70395de02cbfb475df2")
        message(FATAL_ERROR "Pinned FFT capture lifecycle source changed")
    endif()
    file(SHA256 "${TM_TIC80_SOURCE}/src/ext/miniaudio.h" audio_sha)
    if(NOT audio_sha STREQUAL "b468caf317dc32552a9bbdb8c4083bb3d9fa268f5fac502aeb2973e48579400b")
        message(FATAL_ERROR "Pinned miniaudio capture header changed")
    endif()
    file(READ "${original}" code)
    tm_fft_replace(code "#include <stdio.h>" "#include <stdio.h>\n#include <wchar.h>\n#include <math.h>")
    tm_fft_replace(code "#define MINIAUDIO_IMPLEMENTATION" "/* Device implementation lives in a separate translation unit. */")
    tm_fft_replace(code "static ma_spinlock sampleLock = 0;" [=[static ma_spinlock sampleLock = 0;
/* Open/close/enumeration are owned by one control thread. Audio callbacks only
 * touch sampleBuf under sampleLock. The logger outlives the context/device. */
static ma_log captureLog;
static bool logReady, contextReady, deviceReady;

static ma_result tm_fft_context_init(ma_log* log, ma_context* target)
{
    const ma_backend backend = ma_backend_alsa;
    ma_context_config config = ma_context_config_init();
    config.pLog = log;
    /* No null backend or fallback to an unrelated sound provider. */
    return ma_context_init(&backend, 1, &config, target);
}

static void tm_fft_clear(void)
{
    ma_spinlock_lock(&sampleLock);
    memset(sampleBuf, 0, sizeof sampleBuf);
    ma_spinlock_unlock(&sampleLock);
    memset(fftData, 0, sizeof fftData);
    memset(fftSmoothingData, 0, sizeof fftSmoothingData);
    memset(fftNormalizedData, 0, sizeof fftNormalizedData);
    memset(fftNormalizedMaxData, 0, sizeof fftNormalizedMaxData);
    memset(fftRawData, 0, sizeof fftRawData);
    memset(fftRawSmoothingData, 0, sizeof fftRawSmoothingData);
    fPeakMinValue = 0.01f;
    fPeakSmoothing = 0.995f;
    fPeakSmoothValue = 0.0f;
    fAmplification = 1.0f;
}]=])
    # Keep invalid channel values out of both FFT and VQT history. Double
    # precision averaging avoids overflow of finite float stereo inputs.
    tm_fft_replace(code "        *(p++) = samples ? (samples[i * 2] + samples[i * 2 + 1]) / 2.0f : 0.0f;" [=[        float left = samples ? samples[i * 2] : 0.0f;
        float right = samples ? samples[i * 2 + 1] : 0.0f;
        if (!isfinite(left)) left = 0.0f;
        if (!isfinite(right)) right = 0.0f;
        *(p++) = (float)(((double)left + (double)right) * 0.5);]=])
    # Replace lifecycle functions as one checked span; preserve public signatures.
    string(FIND "${code}" "void FFT_EnumerateDevices()" begin)
    string(FIND "${code}" "void FFT_GetFFT(float* _samples)" end)
    if(begin LESS 0 OR end LESS begin)
        message(FATAL_ERROR "Pinned FFT lifecycle span missing")
    endif()
    string(SUBSTRING "${code}" 0 ${begin} prefix)
    string(SUBSTRING "${code}" ${end} -1 suffix)
    set(lifecycle [=[void FFT_EnumerateDevices()
{
#ifndef TIC80_FFT_UNSUPPORTED
    /* Enumeration must not overwrite an active capture context. */
    ma_log log;
    ma_context enumeration;
    if (ma_log_init(NULL, &log) != MA_SUCCESS) return;
    if (ma_log_register_callback(&log, ma_log_callback_init(miniaudioLogCallback, NULL)) != MA_SUCCESS)
        goto done_log;
    if (tm_fft_context_init(&log, &enumeration) != MA_SUCCESS) goto done_log;
    ma_device_info *playback, *capture;
    ma_uint32 playbackCount, captureCount;
    if (ma_context_get_devices(&enumeration, &playback, &playbackCount, &capture, &captureCount) == MA_SUCCESS)
        for (ma_uint32 i = 0; i < captureCount; ++i)
            printf("%u: %s\n", i, capture[i].name);
    ma_context_uninit(&enumeration);
done_log:
    ma_log_uninit(&log);
#endif
}

bool FFT_Open(bool CapturePlaybackDevices, const char* CaptureDeviceSearchString)
{
#ifdef TIC80_FFT_UNSUPPORTED
    return false;
#else
    FFT_Close();
    /* ALSA capture does not implement WASAPI playback loopback. */
    if (CapturePlaybackDevices) return false;
    fftcfg = kiss_fftr_alloc(FFT_SIZE * 2, false, NULL, NULL);
    if (!fftcfg) goto failed;
    if (ma_log_init(NULL, &captureLog) != MA_SUCCESS) goto failed;
    logReady = true;
    if (ma_log_register_callback(&captureLog, ma_log_callback_init(miniaudioLogCallback, NULL)) != MA_SUCCESS)
        goto failed;
    if (tm_fft_context_init(&captureLog, &context) != MA_SUCCESS) goto failed;
    contextReady = true;
    ma_device_info *playback, *capture;
    ma_uint32 playbackCount, captureCount;
    if (ma_context_get_devices(&context, &playback, &playbackCount, &capture, &captureCount) != MA_SUCCESS)
        goto failed;
    ma_device_id* selected = NULL;
    if (CaptureDeviceSearchString && *CaptureDeviceSearchString)
    {
        for (ma_uint32 i = 0; i < captureCount; ++i)
            if (strstr(capture[i].name, CaptureDeviceSearchString))
            { selected = &capture[i].id; break; }
        /* A requested device must not silently select another microphone. */
        if (!selected) goto failed;
    }
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.pDeviceID = selected;
    config.capture.format = ma_format_f32;
    config.capture.channels = 2;
    config.sampleRate = 44100;
    config.dataCallback = OnReceiveFrames;
    if (ma_device_init(&context, &config, &captureDevice) != MA_SUCCESS) goto failed;
    deviceReady = true;
    if (ma_device_start(&captureDevice) != MA_SUCCESS) goto failed;
    fftEnabled = true;
    if (!VQT_Open()) FFT_DebugLog(FFT_LOG_WARNING, "VQT initialization failed; FFT remains available\n");
    return true;
failed:
    FFT_Close();
    return false;
#endif
}

void FFT_Close()
{
#ifndef TIC80_FFT_UNSUPPORTED
    fftEnabled = false;
    /* Uninit joins capture callbacks before clearing shared samples. */
    if (deviceReady) { ma_device_uninit(&captureDevice); deviceReady = false; }
    if (contextReady) { ma_context_uninit(&context); contextReady = false; }
    if (logReady) { ma_log_uninit(&captureLog); logReady = false; }
    VQT_Close();
    kiss_fft_free(fftcfg);
    fftcfg = NULL;
    tm_fft_clear();
#endif
}

]=])
    set(code "${prefix}${lifecycle}${suffix}")
    tm_fft_replace(code "    kiss_fft_cpx out[FFT_SIZE + 1];" [=[    if (!_samples) return;
    if (!fftEnabled || !fftcfg)
    { memset(_samples, 0, FFT_SIZE * sizeof(float)); return; }
    kiss_fft_cpx out[FFT_SIZE + 1];]=])
    tm_fft_replace(code "            FFT_DebugLog(FFT_LOG_TRACE, \"FFT: clamped startFreq to %d\\n\", FFT_SIZE - 1);\n            startFreq = 0;"
        "            FFT_DebugLog(FFT_LOG_TRACE, \"FFT: clamped startFreq to %d\\n\", FFT_SIZE - 1);\n            startFreq = FFT_SIZE - 1;")
    tm_fft_replace(code "    if (count <= 0 || count > AUDIO_BUFFER_SIZE) return;"
        "    if (!samples || count <= 0 || count > AUDIO_BUFFER_SIZE) return;")
    tm_fft_replace(code "        float val = 2.0f * sqrtf(out[i].r * out[i].r + out[i].i * out[i].i);" [=[        float val = 2.0f * hypotf(out[i].r, out[i].i);
        /* An unrepresentable transform must not poison gain or smoothing. */
        if (!isfinite(val)) val = 0.0f;]=])
    tm_fft_replace(code "        _samples[i] = val * fAmplification;" "")
    tm_fft_replace(code "    fAmplification = 1.0f / fPeakSmoothValue;" [=[    fAmplification = 1.0f / fPeakSmoothValue;
    /* Use this spectrum's peak, including startup and rising input levels. */
    for (int i = 0; i < FFT_SIZE; i++) _samples[i] = fftRawData[i] * fAmplification;]=])
    set(staged "${CMAKE_BINARY_DIR}/fft_capture/fft.c")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/fft_capture")
    tm_write_generated("${staged}" "${code}")
    set(vqt_original "${TM_TIC80_SOURCE}/src/ext/vqt.c")
    file(SHA256 "${vqt_original}" vqt_sha)
    if(NOT vqt_sha STREQUAL "95eb9768d638b49e836ce85a4867bf9d2d8bccbc5e0c3f87cbc67350e5829c63")
        message(FATAL_ERROR "Pinned VQT processing source changed")
    endif()
    file(READ "${vqt_original}" vqt_code)
    tm_fft_replace(vqt_code "        vqtData[bin] = sqrt(real * real + imag * imag) * 2.0f;  // Match FFT gain factor"
        "        vqtData[bin] = hypotf(real, imag) * 2.0f;  // Avoid intermediate squared-component overflow.")
    set(vqt_staged "${CMAKE_BINARY_DIR}/fft_capture/vqt.c")
    tm_write_generated("${vqt_staged}" "${vqt_code}")
    add_library(tic80_fft_capture STATIC "${staged}"
        "${TM_TIC80_SOURCE}/src/fftdata.c" "${TM_TIC80_SOURCE}/src/vqtdata.c"
        "${vqt_staged}" "${TM_TIC80_SOURCE}/src/ext/vqt_kernel.c"
        "${TM_TIC80_SOURCE}/src/ext/kiss_fft.c" "${TM_TIC80_SOURCE}/src/ext/kiss_fftr.c")
    target_include_directories(tic80_fft_capture PRIVATE "${TM_TIC80_SOURCE}/src/ext"
        PUBLIC "${TM_TIC80_SOURCE}/src" "${TM_TIC80_SOURCE}/include" "${CMAKE_BINARY_DIR}")
    target_compile_features(tic80_fft_capture PRIVATE c_std_99)
    set(capture_definitions MA_ENABLE_ONLY_SPECIFIC_BACKENDS MA_ENABLE_ALSA MA_NO_NULL
        MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE)
    target_compile_definitions(tic80_fft_capture PUBLIC ${capture_definitions})
    target_link_libraries(tic80_fft_capture PUBLIC m)
    add_library(tic80_capture_device STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/capture_device.c")
    target_include_directories(tic80_capture_device PRIVATE "${TM_TIC80_SOURCE}/src/ext")
    target_compile_definitions(tic80_capture_device PRIVATE ${capture_definitions})
    target_link_libraries(tic80_capture_device PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
    if(TM_ENABLE_FFT_CAPTURE)
        # Drop the original stub/unsafe implementation and duplicate DSP/data
        # objects. The core and diagnostic must use the same repaired backend.
        get_target_property(core_sources tic80core SOURCES)
        foreach(unit fftdata vqtdata ext/fft ext/vqt ext/vqt_kernel ext/kiss_fft ext/kiss_fftr)
            set(source "${TM_TIC80_SOURCE}/src/${unit}.c")
            list(FIND core_sources "${source}" at)
            if(at EQUAL -1)
                message(FATAL_ERROR "Pinned FFT core source missing: ${unit}")
            endif()
            list(REMOVE_ITEM core_sources "${source}")
        endforeach()
        set_property(TARGET tic80core PROPERTY SOURCES "${core_sources}")
        target_link_libraries(tic80core PUBLIC tic80_fft_capture tic80_capture_device)
    endif()
    add_executable(tic80-fft-probe "${CMAKE_CURRENT_SOURCE_DIR}/tools/fft_probe.c")
    target_link_libraries(tic80-fft-probe PRIVATE tic80_fft_capture tic80_capture_device)
    add_executable(fft_capture_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fft_capture_test.c")
    target_compile_options(fft_capture_test PRIVATE -UNDEBUG)
    target_link_libraries(fft_capture_test PRIVATE tic80_fft_capture Threads::Threads)
    target_link_options(fft_capture_test PRIVATE -Wl,--wrap=kiss_fftr_alloc)
    add_test(NAME fft_capture_lifecycle COMMAND fft_capture_test)
    set_tests_properties(fft_capture_lifecycle PROPERTIES TIMEOUT 30)
    add_executable(fft_spectrum_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fft_spectrum_test.c")
    target_compile_options(fft_spectrum_test PRIVATE -UNDEBUG)
    target_link_libraries(fft_spectrum_test PRIVATE tic80_fft_capture Threads::Threads)
    target_link_options(fft_spectrum_test PRIVATE -Wl,--wrap=kiss_fftr_alloc)
    add_test(NAME runtime_fft_spectrum COMMAND fft_spectrum_test)
    set_tests_properties(runtime_fft_spectrum PROPERTIES TIMEOUT 90)
    add_executable(vqt_spectrum_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/vqt_spectrum_test.c")
    target_compile_options(vqt_spectrum_test PRIVATE -UNDEBUG)
    target_link_libraries(vqt_spectrum_test PRIVATE tic80_fft_capture Threads::Threads)
    target_link_options(vqt_spectrum_test PRIVATE -Wl,--wrap=kiss_fftr_alloc)
    add_test(NAME runtime_vqt_spectrum COMMAND vqt_spectrum_test)
    set_tests_properties(runtime_vqt_spectrum PROPERTIES TIMEOUT 90)
    # Own the VQT symbols here; the shared library supplies the same capture,
    # FFT, kernels and state. This exercises the supported whitening switch.
    add_executable(vqt_spectrum_plain_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/vqt_spectrum_test.c" "${vqt_staged}")
    target_include_directories(vqt_spectrum_plain_test PRIVATE "${TM_TIC80_SOURCE}/src/ext")
    target_compile_options(vqt_spectrum_plain_test PRIVATE -UNDEBUG)
    target_compile_definitions(vqt_spectrum_plain_test PRIVATE VQT_SPECTRAL_WHITENING_ENABLED=0)
    target_link_libraries(vqt_spectrum_plain_test PRIVATE tic80_fft_capture Threads::Threads)
    target_link_options(vqt_spectrum_plain_test PRIVATE -Wl,--wrap=kiss_fftr_alloc)
    add_test(NAME runtime_vqt_spectrum_plain COMMAND vqt_spectrum_plain_test)
    set_tests_properties(runtime_vqt_spectrum_plain PROPERTIES TIMEOUT 90)
endfunction()
