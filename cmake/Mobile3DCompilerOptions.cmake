function(mobile3d_apply_compiler_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(MOBILE3D_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Wnon-virtual-dtor
        )
        if(MOBILE3D_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
