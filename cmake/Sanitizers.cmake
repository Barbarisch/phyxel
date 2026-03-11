function(enable_sanitizers project_name)
    # ASan is configured globally in the top-level CMakeLists.txt (before external deps)
    # to ensure ABI consistency across all linked libraries.
    # This function handles per-target sanitizers like TSan.

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        option(ENABLE_TSAN "Enable Thread Sanitizer" OFF)
        if(ENABLE_TSAN)
            target_compile_options(${project_name} PRIVATE -fsanitize=thread)
            target_link_options(${project_name} PRIVATE -fsanitize=thread)
        endif()
    endif()
endfunction()
