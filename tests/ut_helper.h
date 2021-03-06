#pragma once

#include "spec_auto_config.h"
#define LIBVS 1
#define LIBSAIREDIS 2
#define WITH_SAI LIBVS // FIXME: from spec_auto_config.h

#include "gtest/gtest.h"
#include "portal.h"
#include "saispy.h"

#include "check.h"

#if WITH_SAI == LIBVS
#include "sai_vs.h"
#include "sai_vs_state.h"
#endif

#if WITH_SAI == LIBSAIREDIS
#include "hiredis.h"
#include "saihelper.h"
#endif
