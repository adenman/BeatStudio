if(NOT LMMS_BUILD_WIN32)
	return()
endif()

include(BuildPlugin)
build_plugin(vst3effect Vst3Effect.cpp Vst3Effect.h MOCFILES Vst3Effect.h EMBEDDED_RESOURCES logo.svg)
target_link_libraries(vst3effect vst3base)
