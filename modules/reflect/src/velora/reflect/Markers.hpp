#pragma once

// Reflection source markers. They expand to plain C++ (or nothing) and are read by the
// reflection compiler (`velorac reflect`); they carry no runtime dependency.
#define rkattr(...)
#define rkclass class
#define rkstruct struct
#define rkenum enum
#define rkfield(...)
#define rkfunc
#define rkctor
// The c* spellings are equivalent to the rk* ones.
#define cclass class
#define cstruct struct
#define cenum enum
#define cfield(...)
#define cfunc
#define cctor

// UI metadata markers inside a class body (see velora/reflect/Compiler.hpp):
//   rkproperty(Name, Type, get=fn, set=fn, default=value,
//   Measure|Arrange|Paint|Inherited|Content|Attached...) rkevent(Name, Signature, add=fn)
#define rkproperty(...)
#define rkevent(...)
