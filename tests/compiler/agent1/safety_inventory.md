# Safety mechanism inventory

| Mechanism | Component | Enforcement | Modes |
| --- | --- | --- | --- |
| constant OOB / negative index / one-past deref | StaticBoundsCheck.cpp + AstSafetyCheck.cpp | compile-time error | SAFE/STRICT |
| dynamic array index bounds | CPlusRewriter.cpp `__cplus_bounds_check` + runtime | runtime abort | SAFE builds |
| array decay / call extent propagation | AstSafetyCheck.cpp callParamExtents_, stackArrayExtents_, return summaries | compile-time error | SAFE/STRICT |
| signed overflow / divide / shift | AstSafetyCheck.cpp CPLUS-ARITH | compile-time error (STRICT), warning (DEFAULT) | DEFAULT/STRICT |
| UAF / double free / invalid free | AstSafetyCheck.cpp + runtime allocator table | compile-time where provable, runtime for managed | SAFE |
| definite initialization | OwnershipAnalysis.cpp CFG | compile-time error | SAFE |
| RAII cleanup | RaiiLowering.cpp | generated C cleanup on supported exits | SAFE |
| goto/longjmp cleanup bypass | RaiiLowering.cpp | STRICT error when live resource | DEFAULT warn |
| FFI contracts | OwnershipAnalysis.cpp JSON + AstSafetyCheck.cpp | compile-time, unsupported contract fails closed | SAFE |
| sanitizers | Driver.cpp addSanitizerFlags | opt-in compile+link instrumentation | all modes |
| aliases / borrows | AstSafetyCheck.cpp stack alias owner + CPlusRewriter | compile-time error/warn | SAFE |
