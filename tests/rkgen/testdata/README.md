# Reflection regression inputs

These three headers were newly authored during the Velora cleanup. The uploaded
snapshot contained their normalized declaration and metadata goldens but omitted
the source headers. They are project test inputs, not recovered Toolkit originals.

The declarations retain the existing assertions: enum values and implicit struct
fields; nested namespaces, pointer/reference types and accessor annotations;
inheritance, read-only/transient fields, constructors, const/noexcept methods and
static methods. Unreflected accessor declarations make the headers valid C++
without adding reflected methods. No implementation is needed for these parsing
fixtures.

The reference goldens retain the existing declaration and stable-ID expectations;
source locations follow these newly authored headers. Both test executables must
fail when a required header is absent or empty. The frontend reference corpus also
fails when a header produces no reflected types.

C++ syntax and metadata serialization checks do not execute the ANTLR frontend.
The complete acceptance requires the tpkg ANTLR dependencies and running
`velora_rkgen_frontend_test` and `velora_metadata_test` through CTest.
