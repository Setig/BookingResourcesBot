include_guard(GLOBAL)

function(booking_resources_target_link_core target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "booking_resources_target_link_core: TARGET '${target}' is unknown.")
  endif()

  # Prefer the public alias if available.
  set(_br_core_target "BookingResources::Core")
  if(NOT TARGET ${_br_core_target})
    if(TARGET BookingResourcesCore)
      set(_br_core_target "BookingResourcesCore")
    else()
      message(FATAL_ERROR "booking_resources_target_link_core: BookingResources core target was not created.")
    endif()
  endif()

  # BookingResourcesCore is a static archive. Static self-registration relies on translation units
  # being linked even if no symbols are referenced directly.
  if(MSVC)
    target_link_libraries(${target}
      PRIVATE
        ${_br_core_target}
    )
    target_link_options(${target}
      PRIVATE
        "/WHOLEARCHIVE:$<TARGET_FILE:${_br_core_target}>"
    )
  elseif(APPLE)
    # Apple's ld uses -force_load for per-archive force-linking.
    target_link_libraries(${target}
      PRIVATE
        ${_br_core_target}
    )
    target_link_options(${target}
      PRIVATE
        "-Wl,-force_load,$<TARGET_FILE:${_br_core_target}>"
    )
  else()
    target_link_libraries(${target}
      PRIVATE
        -Wl,--whole-archive
        ${_br_core_target}
        -Wl,--no-whole-archive
    )
  endif()
endfunction()


