#pragma once
#include "Shader.hpp"
#include <algorithm>
#include <cmath>

namespace Renderer {
/// Approximate distant-room reflection in an equirectangular texture.
/// Inputs are in world space; normal magnitude need not be one. Compute per
/// vertex before rendering, then interpolate the resulting UVs normally.
/// This is environment mapping, not a live scene reflection or ray trace.
/// Use WRAP addressing; unwrap U around a common anchor on each surface when
/// it straddles the panorama seam. Borrowed texture pixels remain immutable.
inline Vector2 environmentReflectionUV(const Vector3& worldPosition,
                                      const Vector3& worldNormal,
                                      const Vector3& cameraPosition) {
    constexpr float pi=3.14159265358979323846f;
    float nx=float(worldNormal.x),ny=float(worldNormal.y),nz=float(worldNormal.z);
    float vx=float(cameraPosition.x)-worldPosition.x;
    float vy=float(cameraPosition.y)-worldPosition.y;
    float vz=float(cameraPosition.z)-worldPosition.z;
    const float nn=nx*nx+ny*ny+nz*nz, vv=vx*vx+vy*vy+vz*vz;
    if(nn<1e-12f || vv<1e-12f) return {FIXED_POINT_SCALE/2,FIXED_POINT_SCALE/2};
    const float invN=1.f/std::sqrt(nn),invV=1.f/std::sqrt(vv);
    nx*=invN;ny*=invN;nz*=invN;vx*=invV;vy*=invV;vz*=invV;
    const float twiceDot=2.f*(nx*vx+ny*vy+nz*vz);
    const float rx=twiceDot*nx-vx,ry=twiceDot*ny-vy,rz=twiceDot*nz-vz;
    const float u=.5f+std::atan2(rx,rz)/(2*pi);
    const float v=.5f-std::asin(std::clamp(ry,-1.f,1.f))/pi;
    return {int32_t(std::lround(u*FIXED_POINT_SCALE)),
        std::clamp<int32_t>(int32_t(std::lround(v*FIXED_POINT_SCALE)),0,FIXED_POINT_SCALE-1)};
}
inline int32_t unwrapEnvironmentU(int32_t u,int32_t anchor) {
    while(u-anchor>FIXED_POINT_SCALE/2) u-=FIXED_POINT_SCALE;
    while(u-anchor<-FIXED_POINT_SCALE/2) u+=FIXED_POINT_SCALE;
    return u;
}
}
