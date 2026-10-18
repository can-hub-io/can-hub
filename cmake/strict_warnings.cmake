# Warnings are errors in the targets built from our own sources; vendored and
# fetched code (picotls, Monocypher, ngtcp2, SQLite) is left out on purpose.
function(can_hub_strict_warnings)
    foreach(target IN LISTS ARGN)
        target_compile_options(${target} PRIVATE -Werror)
    endforeach()
endfunction()
