// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Andrea Bedini

#pragma once

#include <QtCore/qglobal.h>

#if defined(QTUSBMUXD_STATIC)
#  define QTUSBMUXD_EXPORT
#elif defined(QTUSBMUXD_BUILD_LIBRARY)
#  define QTUSBMUXD_EXPORT Q_DECL_EXPORT
#else
#  define QTUSBMUXD_EXPORT Q_DECL_IMPORT
#endif
