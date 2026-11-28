struct Packet { int h; };
struct Widget { int id; };
struct Res { ~Res() {} int id; };
// ANNOTATE Packet packed_wire
// ANNOTATE Widget cplus_class
// ANNOTATE Res ffi_export cplus_resource
