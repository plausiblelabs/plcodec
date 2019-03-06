This directory contains source and binary dependencies:

ftl
    Description:
      C++11 functional programming library.

    Version:
      Custom fork (https://github.com/plausiblelabs/ftl) of https://github.com/beark/ftl managed
      as a Git submodule.

    License:
      BSD-like

    Modifications:
      Disabled value-type overload of `bind_helper<eitherT<L,M2>>` in either_trans.h; this
      triggers build errors when the value within the target monad is *not* an lvalue
      reference.

XSmallTest
    Description:
      A minimal single-header unit test DSL, compatible with Xcode's XCTest.

    Version:
      ecbbe255eb499f376c83d31528316214b80bbdc2 checked out from https://github.com/landonf/XSmallTest

    License:
      MIT

    Modifications:
      Marked int_xsm_sect_record_record struct as `aligned` to fix alignment-related linker warnings on iOS.
