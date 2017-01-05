/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <http://www.natron.fr/>,
 * Copyright (C) 2016 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "ImagePrivate.h"

#include <cassert>
#include <stdexcept>

#include <QtCore/QDebug>

#include "Engine/OSGLContext.h"
#include "Engine/OSGLFunctions.h"


// disable some warnings due to unused parameters
GCC_DIAG_OFF(unused-parameter)
# if ( ( __GNUC__ * 100) + __GNUC_MINOR__) >= 406
GCC_DIAG_OFF(unused-but-set-variable) // only on gcc >= 4.6
#endif

// NATRON_COPY_CHANNELS_UNPREMULT:
// Repremult R G and B if output is premult and alpha was modified.
// We do not consider it a good thing, since the user explicitely deselected the channels, and expects
// to get the values from input instead.
//#define NATRON_COPY_CHANNELS_UNPREMULT

NATRON_NAMESPACE_ENTER;

template <typename PIX, int maxValue, int srcNComps, int dstNComps, bool doR, bool doG, bool doB, bool doA>
static void
copyUnProcessedChannels_templated(const void* originalImgPtrs[4],
                                  const RectI& originalImgBounds,
                                  void* dstImgPtrs[4],
                                  const RectI& dstBounds,
                                  const RectI& roi,
                                  const TreeRenderNodeArgsPtr& renderArgs)
{

    PIX* dstPixelPtrs[4];
    int dstPixelStride;
    Image::getChannelPointers<char, dstNComps>((PIX**)dstImgPtrs, roi.x1, roi.y1, dstBounds, dstPixelPtrs, &dstPixelStride);

    PIX* srcPixelPtrs[4];
    int srcPixelStride;
    Image::getChannelPointers<char, srcNComps>((PIX**)originalImgPtrs, roi.x1, roi.y1, originalImgBounds, srcPixelPtrs, &srcPixelStride);


    const int dstRowElements = dstPixelStride * dstBounds.width();
    const int srcRowElements = srcPixelStride * originalImgBounds.width();

    for ( int y = roi.y1; y < roi.y2; ++y) {

        if (renderArgs && renderArgs->isRenderAborted()) {
            return;
        }

        for (int x = roi.x1; x < roi.x2; ++x) {

            // be opaque for anything that doesn't contain alpha
            PIX srcA;
            if (srcPixelPtrs[srcNComps - 1]) {
                srcA = maxValue;
            } else {
                srcA = 0;
            }
            if ( ( (srcNComps == 1) || (srcNComps == 4) ) && srcPixelPtrs[srcNComps - 1] ) {
                srcA = *srcPixelPtrs[srcNComps - 1];
#             ifdef DEBUG
                assert(srcA == srcA); // check for NaN
#             endif
            }

#        ifdef NATRON_COPY_CHANNELS_UNPREMULT
            assert(srcNComps == 1 || srcNComps == 4 || !originalPremult); // only A or RGBA can be premult
            assert(dstNComps == 1 || dstNComps == 4 || !premult); // only A or RGBA can be premult

            // Repremult R G and B if output is premult and alpha was modified.
            // We do not consider it a good thing, since the user explicitely deselected the channels, and expects
            // to get the values from input instead.
#           define DOCHANNEL(c)                                                    \
                if (srcNComps == 1 || !src_pixels || c >= srcNComps) {      \
                    dst_pixels[c] = 0;                                      \
                } \
                else if (originalPremult) {                               \
                    if (srcA == 0) {                                        \
                        dst_pixels[c] = src_pixels[c];         /* don't try to unpremult, just copy */ \
                    } \
                    else if (premult) {                                   \
                        if (doA) {                                          \
                            dst_pixels[c] = src_pixels[c];         /* dst will have same alpha as src, just copy src */ \
                        } \
                        else {                                            \
                            dst_pixels[c] = (src_pixels[c] / (float)srcA) * dstAorig;         /* dst keeps its alpha, unpremult src and repremult */ \
                        }                                                   \
                    } \
                    else {                                                \
                        dst_pixels[c] = (src_pixels[c] / (float)srcA) * maxValue;         /* dst is not premultiplied, unpremult src */ \
                    }                                                       \
                } \
                else {                                                    \
                    if (premult) {                                          \
                        if (doA) {                                          \
                            dst_pixels[c] = (src_pixels[c] / (float)maxValue) * srcA;         /* dst will have same alpha as src, just premult src with its alpha */ \
                        } \
                        else {                                            \
                            dst_pixels[c] = (src_pixels[c] / (float)maxValue) * dstAorig;         /* dst keeps its alpha, premult src with dst's alpha */ \
                        }                                                   \
                    } \
                    else {                                                \
                        dst_pixels[c] = src_pixels[c];         /* neither src nor dst is not premultiplied */ \
                    }                                                       \
                }

            PIX dstAorig = maxValue;
#         else // !NATRON_COPY_CHANNELS_UNPREMULT

            // Just copy the channels, after all if the user unchecked a channel,
            // we do not want to change the values behind his back.
            // Rather we display a warning in  the GUI.

#           define DOCHANNEL(c) *dstPixelPtrs[c] = (c >= srcNComps || !srcPixelPtrs[c]) ? 0 : *srcPixelPtrs[c];

#         endif // !NATRON_COPY_CHANNELS_UNPREMULT

#             ifdef NATRON_COPY_CHANNELS_UNPREMULT
            if ( (dstNComps == 1) || (dstNComps == 4) ) {
                dstAorig = *dstPixelPtrs[dstNComps - 1];
#             ifdef DEBUG
                assert(dstAorig == dstAorig); // check for NaN
#             endif
            }
#             endif // NATRON_COPY_CHANNELS_UNPREMULT

            if (doR) {
#             ifdef DEBUG
                assert(!srcPixelPtrs[0] || *srcPixelPtrs[0] == *srcPixelPtrs[0]); // check for NaN
                assert(*dstPixelPtrs[0] == *dstPixelPtrs[0]); // check for NaN
#             endif

                DOCHANNEL(0);

#             ifdef DEBUG
                assert(*dstPixelPtrs[0] == *dstPixelPtrs[0]); // check for NaN
#             endif
            }

            if (doG) {
#             ifdef DEBUG
                assert(!srcPixelPtrs[1] || *srcPixelPtrs[1] == *srcPixelPtrs[1]); // check for NaN
                assert(*dstPixelPtrs[1] == *dstPixelPtrs[1]); // check for NaN
#             endif

                DOCHANNEL(1);

#             ifdef DEBUG
                assert(*dstPixelPtrs[1] == *dstPixelPtrs[1]); // check for NaN
#             endif           
            }

            if (doB) {
#             ifdef DEBUG
                assert(!srcPixelPtrs[2] || *srcPixelPtrs[2] == *srcPixelPtrs[2]); // check for NaN
                assert(*dstPixelPtrs[2] == *dstPixelPtrs[2]); // check for NaN
#             endif

                DOCHANNEL(2);

#             ifdef DEBUG
                assert(*dstPixelPtrs[2] == *dstPixelPtrs[2]); // check for NaN
#             endif
            }

            if (doA) {

#             ifdef NATRON_COPY_CHANNELS_UNPREMULT
                if (premult) {
                    if (dstAorig != 0) {
                        // unpremult, then premult
                        if ( (dstNComps >= 2) && !doR ) {
                            dst_pixels[0] = (dst_pixels[0] / (float)dstAorig) * srcA;
#                         ifdef DEBUG
                            assert(dst_pixels[0] == dst_pixels[0]); // check for NaN
#                         endif
                        }
                        if ( (dstNComps >= 2) && !doG ) {
                            dst_pixels[1] = (dst_pixels[1] / (float)dstAorig) * srcA;
#                         ifdef DEBUG
                            assert(dst_pixels[1] == dst_pixels[1]); // check for NaN
#                         endif
                        }
                        if ( (dstNComps >= 2) && !doB ) {
                            dst_pixels[2] = (dst_pixels[2] / (float)dstAorig) * srcA;
#                         ifdef DEBUG
                            assert(dst_pixels[2] == dst_pixels[2]); // check for NaN
#                         endif
                        }
                    }
                }
#             endif // NATRON_COPY_CHANNELS_UNPREMULT

                if ( (dstNComps == 1) || (dstNComps == 4) ) {
#                 ifdef DEBUG
                    assert(srcA == srcA); // check for NaN
#                 endif
                    *dstPixelPtrs[dstNComps - 1] = srcA;
                }
            } // doA

            // increment pixel pointers
            for (int c = 0; c < 4; ++c) {
                if (srcPixelPtrs[c]) {
                    srcPixelPtrs[c] += srcPixelStride;
                }
                if (dstPixelPtrs[c]) {
                    dstPixelPtrs[c] += dstPixelStride;
                }
            }

        } // for each pixel on a scan-line

        // Remove what was done in the iteration and go to the next scan-line
        for (int c = 0; c < 4; ++c) {
            if (srcPixelPtrs[c]) {
                srcPixelPtrs[c] += (srcRowElements - roi.width() * srcPixelStride);
            }
            if (dstPixelPtrs[c]) {
                dstPixelPtrs[c] += (dstRowElements - roi.width() * dstPixelStride);
            }
        }
        
    } // for each scan-line
} // copyUnProcessedChannels_templated



template <typename PIX, int maxValue, int srcNComps, int dstNComps>
static void
copyUnProcessedChannels_nonTemplated(const void* originalImgPtrs[4],
                                     const RectI& originalImgBounds,
                                     void* dstImgPtrs[4],
                                     const RectI& dstBounds,
                                     const std::bitset<4> processChannels,
                                     const RectI& roi,
                                     const TreeRenderNodeArgsPtr& renderArgs)
{

    const bool doR = !processChannels[0] && (dstNComps >= 2);
    const bool doG = !processChannels[1] && (dstNComps >= 2);
    const bool doB = !processChannels[2] && (dstNComps >= 3);
    const bool doA = !processChannels[3] && (dstNComps == 1 || dstNComps == 4);

    PIX* dstPixelPtrs[4];
    int dstPixelStride;
    Image::getChannelPointers<char, dstNComps>((PIX**)dstImgPtrs, roi.x1, roi.y1, dstBounds, dstPixelPtrs, &dstPixelStride);

    PIX* srcPixelPtrs[4];
    int srcPixelStride;
    Image::getChannelPointers<char, srcNComps>((PIX**)originalImgPtrs, roi.x1, roi.y1, originalImgBounds, srcPixelPtrs, &srcPixelStride);


    const int dstRowElements = dstPixelStride * dstBounds.width();
    const int srcRowElements = srcPixelStride * originalImgBounds.width();


    for ( int y = roi.y1; y < roi.y2; ++y) {

        if (renderArgs && renderArgs->isRenderAborted()) {
            return;
        }


        for (int x = roi.x1; x < roi.x2; ++x) {

            // Be opaque for anything that doesn't contain alpha
            PIX srcA;
            if (srcPixelPtrs[srcNComps - 1]) {
                srcA = maxValue;
            } else {
                srcA = 0;
            }

            if ( ( (srcNComps == 1) || (srcNComps == 4) ) && srcPixelPtrs[3] ) {
                srcA = *srcPixelPtrs[srcNComps - 1];
#             ifdef  DEBUG
                assert(srcA == srcA); // check for NaN
#             endif
            }
#         ifdef NATRON_COPY_CHANNELS_UNPREMULT
            PIX dstAorig = maxValue;
#         endif
            if ( (dstNComps == 1) || (dstNComps == 4) ) {
#             ifdef NATRON_COPY_CHANNELS_UNPREMULT
                dstAorig = *dst_pixels[dstNComps - 1];
#             ifdef DEBUG
                assert(dstAorig == dstAorig); // check for NaN
#             endif
#             endif
            }
            if (doR) {
#             ifdef DEBUG
                assert(!srcPixelPtrs[0] || *srcPixelPtrs[0] == *srcPixelPtrs[0]); // check for NaN
                assert(*dstPixelPtrs[0] == *dstPixelPtrs[0]); // check for NaN
#             endif

                DOCHANNEL(0);

#             ifdef DEBUG
                assert(*dstPixelPtrs[0] == *dstPixelPtrs[0]); // check for NaN
#             endif
            }
            if (doG) {
#             ifdef DEBUG
                assert(!srcPixelPtrs[1] || *srcPixelPtrs[1] == *srcPixelPtrs[1]); // check for NaN
                assert(*dstPixelPtrs[1] == *dstPixelPtrs[1]); // check for NaN
#             endif

                DOCHANNEL(1);

#             ifdef DEBUG
                assert(*dstPixelPtrs[1] == *dstPixelPtrs[1]); // check for NaN
#             endif
            }
            if (doB) {
#             ifdef DEBUG
                assert(!srcPixelPtrs[2] || *srcPixelPtrs[2] == *srcPixelPtrs[2]); // check for NaN
                assert(*dstPixelPtrs[2] == *dstPixelPtrs[2]); // check for NaN
#             endif

                DOCHANNEL(2);

#             ifdef DEBUG
                assert(*dstPixelPtrs[2] == *dstPixelPtrs[2]); // check for NaN
#             endif
            }
            if (doA) {
#             ifdef NATRON_COPY_CHANNELS_UNPREMULT
                if (premult) {
                    if (dstAorig != 0) {
                        // unpremult, then premult
                        if ( (dstNComps >= 2) && !doR ) {
                            dst_pixels[0] = (dst_pixels[0] / (float)dstAorig) * srcA;
#                         ifdef DEBUG
                            assert(dst_pixels[0] == dst_pixels[0]); // check for NaN
#                         endif
                        }
                        if ( (dstNComps >= 2) && !doG ) {
                            dst_pixels[1] = (dst_pixels[1] / (float)dstAorig) * srcA;
#                         ifdef DEBUG
                            assert(dst_pixels[1] == dst_pixels[1]); // check for NaN
#                         endif
                        }
                        if ( (dstNComps >= 2) && !doB ) {
                            dst_pixels[2] = (dst_pixels[2] / (float)dstAorig) * srcA;
#                         ifdef DEBUG
                            assert(dst_pixels[2] == dst_pixels[2]); // check for NaN
#                         endif
                        }
                    }
                }
#              endif // NATRON_COPY_CHANNELS_UNPREMULT
                // coverity[dead_error_line]

#              ifdef DEBUG
                assert(srcA == srcA); // check for NaN
#              endif
                *dstPixelPtrs[dstNComps - 1] = srcA;
            } // doA

            // increment pixel pointers
            for (int c = 0; c < 4; ++c) {
                if (srcPixelPtrs[c]) {
                    srcPixelPtrs[c] += srcPixelStride;
                }
                if (dstPixelPtrs[c]) {
                    dstPixelPtrs[c] += dstPixelStride;
                }
            }
        } // for each pixels in a scan-line

        // Remove what was done in the iteration and go to the next scan-line
        for (int c = 0; c < 4; ++c) {
            if (srcPixelPtrs[c]) {
                srcPixelPtrs[c] += (srcRowElements - roi.width() * srcPixelStride);
            }
            if (dstPixelPtrs[c]) {
                dstPixelPtrs[c] += (dstRowElements - roi.width() * dstPixelStride);
            }
        }
    } // for each scan-line
} // copyUnProcessedChannels_nonTemplated




template <typename PIX, int maxValue, int srcNComps, int dstNComps>
static void
copyUnProcessedChannelsForDstComponents(const void* originalImgPtrs[4],
                                        const RectI& originalImgBounds,
                                        void* dstImgPtrs[4],
                                        const RectI& dstBounds,
                                        const std::bitset<4> processChannels,
                                        const RectI& roi,
                                        const TreeRenderNodeArgsPtr& renderArgs)
{

    const bool doR = !processChannels[0] && (dstNComps >= 2);
    const bool doG = !processChannels[1] && (dstNComps >= 2);
    const bool doB = !processChannels[2] && (dstNComps >= 3);
    const bool doA = !processChannels[3] && (dstNComps == 1 || dstNComps == 4);

    if (dstNComps == 1) {
        if (doA) {
            copyUnProcessedChannels_templated<PIX, maxValue, srcNComps, dstNComps, false, false, false, true>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, roi, renderArgs);     // RGB were processed, copy A
        } else {
            copyUnProcessedChannels_templated<PIX, maxValue, srcNComps, dstNComps, false, false, false, false>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, roi, renderArgs);     // RGBA were processed, only do premult
        }
    } else {
        assert(2 <= dstNComps && dstNComps <= 4);
        if (doR) {
            if (doG) {
                if ( (dstNComps >= 3) && doB ) {
                    if ( (dstNComps >= 4) && doA ) {
                        copyUnProcessedChannels_templated<PIX, maxValue, srcNComps, dstNComps, true, true, true, true>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, roi, renderArgs); // none were processed, only do premult
                    } else {
                        copyUnProcessedChannels_templated<PIX, maxValue, srcNComps, dstNComps, true, true, true, false>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, roi, renderArgs); // A was processed
                    }
                } else {
                    if ( (dstNComps >= 4) && doA ) {
                        copyUnProcessedChannels_templated<PIX, maxValue, srcNComps, dstNComps, true, true, false, true>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, roi, renderArgs); // B was processed
                    } else {
                        copyUnProcessedChannels_nonTemplated<PIX, maxValue, srcNComps, dstNComps>(/*true, true, false, false, */ originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, processChannels, roi, renderArgs); // BA were processed (rare)
                    }
                }
            } else {
                if ( (dstNComps >= 3) && doB ) {
                    if ( (dstNComps >= 4) && doA ) {
                        copyUnProcessedChannels_templated<PIX, maxValue, srcNComps, dstNComps, true, false, true, true>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, roi, renderArgs); // G was processed
                    } else {
                        copyUnProcessedChannels_nonTemplated<PIX, maxValue, srcNComps, dstNComps>(/*true, false, true, false, */ originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, processChannels, roi, renderArgs); // GA were processed (rare)
                    }
                } else {
                    //if (dstNComps >= 4 && doA) {
                    copyUnProcessedChannels_nonTemplated<PIX, maxValue, srcNComps, dstNComps>(/*true, false, false, true, */ originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, processChannels, roi, renderArgs);    // GB were processed (rare)
                    //} else {
                    //    copyUnProcessedChannelsForChannels<PIX, maxValue, srcNComps, dstNComps>(/*true, false, false, false, */processChannels, premult, roi, originalImage, originalPremult, ignorePremult); // GBA were processed (rare)
                    //}
                }
            }
        } else {
            if (doG) {
                if ( (dstNComps >= 3) && doB ) {
                    if ( (dstNComps >= 4) && doA ) {
                        copyUnProcessedChannels_templated<PIX, maxValue, srcNComps, dstNComps, false, true, true, true>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, roi, renderArgs); // R was processed
                    } else {
                        copyUnProcessedChannels_nonTemplated<PIX, maxValue, srcNComps, dstNComps>(/*false, true, true, false, */ originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, processChannels, roi, renderArgs); // RA were processed (rare)
                    }
                } else {
                    //if (dstNComps >= 4 && doA) {
                    copyUnProcessedChannels_nonTemplated<PIX, maxValue, srcNComps, dstNComps>(/*false, true, false, true, */ originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, processChannels, roi, renderArgs);    // RB were processed (rare)
                    //} else {
                    //    copyUnProcessedChannelsForChannels<PIX, maxValue, srcNComps, dstNComps>(/*false, true, false, false, */originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, processChannels, roi); // RBA were processed (rare)
                    //}
                }
            } else {
                if ( (dstNComps >= 3) && doB ) {
                    //if (dstNComps >= 4 && doA) {
                    copyUnProcessedChannels_nonTemplated<PIX, maxValue, srcNComps, dstNComps>(/*false, false, true, true, */originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, processChannels, roi, renderArgs);    // RG were processed (rare)
                    //} else {
                    //    copyUnProcessedChannelsForChannels<PIX, maxValue, srcNComps, dstNComps>(/*false, false, true, false, */originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, processChannels, roi); // RGA were processed (rare)
                    //}
                } else {
                    if ( (dstNComps >= 4) && doA ) {
                        copyUnProcessedChannels_templated<PIX, maxValue, srcNComps, dstNComps, false, false, false, true>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, roi, renderArgs); // RGB were processed
                    } else {
                        copyUnProcessedChannels_templated<PIX, maxValue, srcNComps, dstNComps, false, false, false, false>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, roi, renderArgs); // RGBA were processed
                    }
                }
            }
        }
    }
} // Image::copyUnProcessedChannelsForComponents

template <typename PIX, int maxValue, int srcNComps>
static void
copyUnProcessedChannelsForSrcComps(const void* originalImgPtrs[4],
                                   const RectI& originalImgBounds,
                                   void* dstImgPtrs[4],
                                   int dstImgNComps,
                                   const RectI& dstBounds,
                                   const std::bitset<4> processChannels,
                                   const RectI& roi,
                                   const TreeRenderNodeArgsPtr& renderArgs)
{
    switch (dstImgNComps) {
        case 1:
            copyUnProcessedChannelsForSrcComps<PIX, maxValue, srcNComps, 1>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, processChannels, roi, renderArgs);
            break;
        case 2:
            copyUnProcessedChannelsForSrcComps<PIX, maxValue, srcNComps, 2>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, processChannels, roi, renderArgs);
            break;
        case 3:
            copyUnProcessedChannelsForSrcComps<PIX, maxValue, srcNComps, 3>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, processChannels, roi, renderArgs);
            break;
        case 4:
            copyUnProcessedChannelsForSrcComps<PIX, maxValue, srcNComps, 4>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstBounds, processChannels, roi, renderArgs);
            break;
        default:
            assert(false);
            break;
    }
}

template <typename PIX, int maxValue>
static void
copyUnProcessedChannelsForDepth(const void* originalImgPtrs[4],
                                const RectI& originalImgBounds,
                                int originalImgNComps,
                                void* dstImgPtrs[4],
                                int dstImgNComps,
                                const RectI& dstBounds,
                                const std::bitset<4> processChannels,
                                const RectI& roi,
                                const TreeRenderNodeArgsPtr& renderArgs)
{

    switch (originalImgNComps) {
        case 0:
            copyUnProcessedChannelsForSrcComps<PIX, maxValue, 0>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstImgNComps, dstBounds, processChannels, roi, renderArgs);
            break;
        case 1:
            copyUnProcessedChannelsForSrcComps<PIX, maxValue, 1>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstImgNComps, dstBounds, processChannels, roi, renderArgs);
            break;
        case 2:
            copyUnProcessedChannelsForSrcComps<PIX, maxValue, 2>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstImgNComps, dstBounds, processChannels, roi, renderArgs);
            break;
        case 3:
            copyUnProcessedChannelsForSrcComps<PIX, maxValue, 3>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstImgNComps, dstBounds, processChannels, roi, renderArgs);
            break;
        case 4:
            copyUnProcessedChannelsForSrcComps<PIX, maxValue, 4>(originalImgPtrs, originalImgBounds, dstImgPtrs, dstImgNComps,dstBounds, processChannels, roi, renderArgs);
            break;
        default:
            assert(false);
            break;
    }
} // Image::copyUnProcessedChannelsForDepth

void
ImagePrivate::copyUnprocessedChannelsCPU(const void* originalImgPtrs[4],
                                         const RectI& originalImgBounds,
                                         int originalImgNComps,
                                         void* dstImgPtrs[4],
                                         ImageBitDepthEnum dstImgBitDepth,
                                         int dstImgNComps,
                                         const RectI& dstBounds,
                                         const std::bitset<4> processChannels,
                                         const RectI& roi,
                                         const TreeRenderNodeArgsPtr& renderArgs)
{
    switch (dstImgBitDepth) {
        case eImageBitDepthByte:
            copyUnProcessedChannelsForDepth<unsigned char, 255>(originalImgPtrs, originalImgBounds, originalImgNComps, dstImgPtrs, dstImgNComps, dstBounds, processChannels, roi, renderArgs);
            break;
        case eImageBitDepthFloat:
            copyUnProcessedChannelsForDepth<float, 1>(originalImgPtrs, originalImgBounds, originalImgNComps, dstImgPtrs, dstImgNComps, dstBounds, processChannels, roi, renderArgs);
            break;
        case eImageBitDepthShort:
            copyUnProcessedChannelsForDepth<unsigned short, 65535>(originalImgPtrs, originalImgBounds, originalImgNComps, dstImgPtrs, dstImgNComps, dstBounds, processChannels, roi, renderArgs);
            break;

        case eImageBitDepthHalf:
        case eImageBitDepthNone:

            break;
    }
}



template <typename GL>
void
copyUnProcessedChannelsGLInternal(const GLCacheEntryPtr& originalTexture,
                                  const GLCacheEntryPtr& dstTexture,
                                  const std::bitset<4> processChannels,
                                  const RectI& roi,
                                  const OSGLContextPtr& glContext)
{

    GLShaderBasePtr shader = glContext->getOrCreateCopyUnprocessedChannelsShader(processChannels[0], processChannels[1], processChannels[2], processChannels[3]);
    assert(shader);
    GLuint fboID = glContext->getOrCreateFBOId();

    int target = dstTexture->getGLTextureTarget();
    int dstTexID = dstTexture->getGLTextureID();
    int originalTexID = 0;
    if (originalTexture) {
        originalTexID = originalTexture->getGLTextureID();
    }
    GL::BindFramebuffer(GL_FRAMEBUFFER, fboID);
    GL::Enable(target);
    GL::ActiveTexture(GL_TEXTURE0);
    GL::BindTexture( target, dstTexID );

    GL::TexParameteri (target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    GL::TexParameteri (target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GL::TexParameteri (target, GL_TEXTURE_WRAP_S, GL_REPEAT);
    GL::TexParameteri (target, GL_TEXTURE_WRAP_T, GL_REPEAT);

    GL::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, dstTexID, 0 /*LoD*/);
    glCheckFramebufferError(GL);
    glCheckError(GL);
    GL::ActiveTexture(GL_TEXTURE1);
    GL::BindTexture( target, originalTexID );

    GL::TexParameteri (target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    GL::TexParameteri (target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GL::TexParameteri (target, GL_TEXTURE_WRAP_S, GL_REPEAT);
    GL::TexParameteri (target, GL_TEXTURE_WRAP_T, GL_REPEAT);

    const RectI& dstBounds = dstTexture->getBounds();
    const RectI& srcBounds = originalTexture ? originalTexture->getBounds() : dstBounds;

    shader->bind();
    shader->setUniform("originalImageTex", 1);
    shader->setUniform("outputImageTex", 0);
    OfxRGBAColourF procChannelsV = {
        processChannels[0] ? 1.f : 0.f,
        processChannels[1] ? 1.f : 0.f,
        processChannels[2] ? 1.f : 0.f,
        processChannels[3] ? 1.f : 0.f
    };
    shader->setUniform("processChannels", procChannelsV);
    OSGLContext::applyTextureMapping<GL>(srcBounds, dstBounds, roi);
    shader->unbind();

    glCheckError(GL);
    GL::BindTexture(target, 0);
    GL::ActiveTexture(GL_TEXTURE0);
    GL::BindTexture(target, 0);
    glCheckError(GL);

} // copyUnProcessedChannelsGLInternal

void
ImagePrivate::copyUnprocessedChannelsGL(const GLCacheEntryPtr& originalTexture,
                                        const GLCacheEntryPtr& dstTexture,
                                        const std::bitset<4> processChannels,
                                        const RectI& roi)
{
    OSGLContextPtr glContext = dstTexture->getOpenGLContext();
    // Save the current context
    OSGLContextSaver saveCurrentContext;

    {
        // Ensure this context is attached
        OSGLContextAttacherPtr contextAttacher = OSGLContextAttacher::create(glContext);
        contextAttacher->attach();

        if (glContext->isGPUContext()) {
            copyUnProcessedChannelsGLInternal<GL_GPU>(originalTexture, dstTexture, processChannels, roi, glContext);
        } else {
            copyUnProcessedChannelsGLInternal<GL_CPU>(originalTexture, dstTexture, processChannels, roi, glContext);
        }
    }
}


NATRON_NAMESPACE_EXIT;
