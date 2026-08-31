// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "renderBuffer.h"
#include "renderDelegate.h"
#include "camera.h"
#include "ValueConverter.h"
#include "HdmLog.h"
#include "tokens.h"

#include "pxr/base/work/loops.h"

#include <iostream>
#include <cmath>

#include <fstream>

using namespace pxr;

namespace hdMoonray {

void
HdMoonray_RenderBuffer::Sync(HdSceneDelegate* sceneDelegate,
                   HdRenderParam* renderParam,
                   HdDirtyBits* dirtyBits)
{
    const SdfPath& id = GetId();
    hdmLogSyncStart("RenderBuffer", id, dirtyBits);

    mRenderDelegate = &HdMoonray_RenderDelegate::get(renderParam);
    HdRenderBuffer::Sync(sceneDelegate, renderParam, dirtyBits); // this calls Allocate()
    hdmLogSyncEnd(id);
}

bool
HdMoonray_RenderBuffer::Allocate(const GfVec3i& dimensions, HdFormat format, bool multiSampled)
{
    hdmLogRenderBuffer("Allocate",GetId());

    if (dimensions[2] != 1) {
        Logger::error(GetId(), ": dimensions ", dimensions, " unsupported");
        hdmLogRenderBuffer("EndAllocateErr",GetId());
        return false;
    }

    mFormat = format;

    PixelSize request;
    switch (format) {
        case HdFormatInt32:
        case HdFormatFloat32:
            request.mChannels = 1; break;
        case HdFormatFloat32Vec2:
            request.mChannels = 2; break;
        case HdFormatFloat32Vec3:
            request.mChannels = 3; break;
        case HdFormatFloat32Vec4:
            request.mChannels = 4; break;
        default:
            Logger::error(GetId(), ": unknown format ", format);
            hdmLogRenderBuffer("EndAllocateErr",GetId());
            return false;
    }
    request.mWidth = dimensions[0];
    request.mHeight = dimensions[1];

    mRenderDelegate->applySettings();
    bool ret = mRenderDelegate->renderer().allocate(mRenderVar.renderOutput(), mPixelData, request);
    hdmLogRenderBuffer("EndAllocate",GetId());
    return ret;
}

void
HdMoonray_RenderBuffer::bind(const HdRenderPassAovBinding& aovBinding, const HdMoonray_Camera* camera)
{
    mNear = camera->getNear();
    mFar = camera->getFar();

    if (aovBinding.clearValue.IsHolding<GfVec4f>()) {
        GfVec4f v = aovBinding.clearValue.Get<GfVec4f>();
        if (v != clearValue) {
            clearValue = v;
            mPixelData.filmActivity = ~0; // make it recomposite even if render has finished
        }
    }

    if (bound()) {
        return; // Hydra does not reuse render buffers for different aovs, so assume it is unchanged
    }

    hdmLogRenderBuffer("Bind", GetId());
    mBound = true;

    mRenderVar.setAov(aovBinding, mRenderDelegate);

    hdmLogRenderBuffer("EndBind", GetId());
}

bool
HdMoonray_RenderBuffer::IsConverged() const
{
    return true;
}

void
HdMoonray_RenderBuffer::Resolve()
{
    if (not bound()) {
        // Houdini does this, so don't treat as an error
        return;
    }

    // See if the old image is ok
    // Return what we already have (which might be the initial buffer set by Allocate)
    Renderer& renderer = mRenderDelegate->renderer();
    if (not renderer.resolve(mRenderVar.renderOutput(), mPixelData)) {
        return;
    }

    hdmLogRenderBuffer("Resolve", GetId());
    switch (mPixelData.mChannels) {
    case 1:
        if (mRenderVar.isOpenGLDepth()) {
            // Houdini is able to accept the linear depth buffer (see ../houdini/UsdRenderers.json)
            if (mRenderDelegate->options().isHoudini()) break;
            // Calculate the OpenGL style depth (near=0, far=1) from the linear depth
            const float n = mNear;
            const float f = mFar;
            const float A = ((f+n)/(f-n) + 1)/2;
            const size_t count = mPixelData.mWidth * mPixelData.mHeight;
            float* buffer = reinterpret_cast<float*>(mPixelData.mData);
            WorkParallelForN(count, [buffer, n, A](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i) {
                        float z = buffer[i];
                        buffer[i] = z > n ? A * (1.0f - n/z) : 0.0f;
                    }
                });
        } else if (mFormat == HdFormatInt32) {
            // Convert float to int, for ids.
            const size_t count = mPixelData.mWidth * mPixelData.mHeight;
            const float* inbuffer = reinterpret_cast<const float*>(mPixelData.mData);
            int32_t* outbuffer;
            if (mRenderVar.numExtraOutputs() > 0) {
                // if we need to add in extra outputs, we need a separate buffer for the int data
                intBuffer.resize(count);
                outbuffer = intBuffer.data();
            } else { 
                // we can write over the float data with ints
                outbuffer = reinterpret_cast<int32_t*>(mPixelData.mData);
            }
            const float emptyValue = mRenderVar.emptyValue();
            WorkParallelForN(count, [inbuffer, outbuffer, emptyValue](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i) {
                        // inbuffer contains values that display as "inf" and "-0.0" : these become
                        // the minimum value when cast to int32. We can't use std::isfinite, because
                        // we have -ffast-math enabled.
                        int32_t intval = inbuffer[i];
                        if (intval == std::numeric_limits<int32_t>::min())
                            outbuffer[i] = emptyValue;
                        else
                            outbuffer[i] = intval;
                    }
            });

            // add in any extra outputs (used for nested instancing)
            for (size_t i = 0; i < mRenderVar.numExtraOutputs(); ++i) {
                // need to force update, since we are reusing the same buffer
                renderer.resolve(mRenderVar.extraOutputs(i), mPixelData, true);
                inbuffer = reinterpret_cast<const float*>(mPixelData.mData);
                pxr::WorkParallelForN(count, [inbuffer, outbuffer](size_t begin, size_t end) {
                        for (size_t i = begin; i < end; ++i) {
                            int32_t intval = inbuffer[i];
                            if (intval != std::numeric_limits<int32_t>::min())
                                outbuffer[i] += intval; 
                        }
                });
            }

            // uncomment this to write out the int buffer to a file for debugging
            //const std::string filename = "/tmp/" + mAovName.GetString() + "-" + std::to_string(mPixelData.mWidth) + "x" + std::to_string(mPixelData.mHeight) + ".bin";
            //std::cout << "Writing " << filename << std::endl;
            //std::ofstream out(filename, std::ios::binary);
            //out.write(reinterpret_cast<const char*>(outbuffer), count * sizeof(int32_t));
            //out.close();

            mPixelData.mData = outbuffer;

        } else {
            mFormat = HdFormatFloat32;
        }
        break;
    case 2:
        mFormat = HdFormatFloat32Vec2;
        break;
    case 3:
        mFormat = HdFormatFloat32Vec3;
        break;
    case 4:
        mFormat = HdFormatFloat32Vec4;

        // alpha compositing over background color must be done by delegate
        if (clearValue[3] > 0.0f) {
            const size_t count = mPixelData.mWidth * mPixelData.mHeight;
            typedef float v4sf __attribute__ ((vector_size (16)));
            v4sf* buffer = reinterpret_cast<v4sf*>(mPixelData.mData);
            if (clearValue[0] || clearValue[1] || clearValue[2] || clearValue[3] < 1.0f) {
                const v4sf& cv = reinterpret_cast<const v4sf&>(clearValue[0]);
                WorkParallelForN(count, [buffer, cv](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i) {
                        v4sf& pixel = *(buffer + i);
                        if (pixel[3] < 1.0f) {
                            if (pixel[3] > 0.0f) {
                                const float m = 1.0f - pixel[3];
                                pixel += cv * m;
                            } else {
                                pixel += cv;
                            }
                        }
                    }
                });
            } else {
                // composite is simpler when clearValue is opaque black
                WorkParallelForN(count, [buffer](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i) {
                        v4sf& pixel = *(buffer + i);
                        pixel[3] = 1.0f;
                    }
                });
            }
        }

        break;
    default:
        Logger::error(GetId(), ": unknown channel count ", mPixelData.mChannels);
        break;
    }
    hdmLogRenderBuffer("EndResolve", GetId());
}

void
HdMoonray_RenderBuffer::Finalize(HdRenderParam* renderParam)
{

    hdmLogRenderBuffer("Finalize", GetId());
    mRenderVar.finalize();
    HdRenderBuffer::Finalize(renderParam);
    hdmLogRenderBuffer("EndFinalize", GetId());
}

// Called *after* Finalize()
void
HdMoonray_RenderBuffer::_Deallocate()
{
    hdmLogRenderBuffer("_Deallocate", GetId());

    if (mPixelData.mData) {
        mRenderDelegate->renderer().deallocate(mRenderVar.renderOutput(), mPixelData);
        mPixelData.vec.clear();
        mPixelData.vpb.cleanUp();
        intBuffer.clear();
        mPixelData.mData = nullptr;
    }
    hdmLogRenderBuffer("End_Deallocate", GetId());
}

}
