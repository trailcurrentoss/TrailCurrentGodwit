# protobuf_generate() runs the protobuf::protoc target, which the target
# protobuf's CMake package does not export. Point it at the host's protoc.
if(NOT TARGET protobuf::protoc)
  add_executable(protobuf::protoc IMPORTED GLOBAL)
  set_target_properties(protobuf::protoc PROPERTIES IMPORTED_LOCATION "${Protobuf_PROTOC_EXECUTABLE}")
endif()
