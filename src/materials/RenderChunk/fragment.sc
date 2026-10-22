$input v_color0, v_color1, v_fog, v_refl, v_texcoord0, v_lightmapUV, v_extra, v_trip

#include <bgfx_shader.sh>
#include <newb/main.sh>

#ifdef NL_TRIP
  uniform vec4 FogColor;
  uniform vec4 FogAndDistanceControl;
  uniform vec4 ViewPositionAndTime;
  uniform vec4 CameraPosition;
#endif

SAMPLER2D_AUTOREG(s_MatTexture);
SAMPLER2D_AUTOREG(s_SeasonsTexture);
SAMPLER2D_AUTOREG(s_LightMapTexture);

void main() {
  #if defined(DEPTH_ONLY_OPAQUE) || defined(DEPTH_ONLY) || defined(INSTANCING)
    gl_FragColor = vec4(1.0,1.0,1.0,1.0);
    return;
  #endif

  #ifdef NL_TRIP
    TripState trip = tripDecode(FogColor.rgb, FogAndDistanceControl.xy);
    float tz = ViewPositionAndTime.w;
  #endif

  // Seelenreich: Fließen verschiebt die Textur-UV (Stufe 2 ff.), innerhalb der Atlaskachel
  vec2 uv = v_texcoord0;
  #if defined(NL_TRIP) && defined(NL_TRIP_TIEFE)
    uv = tripFliessen(s_MatTexture, uv, v_trip.w > 0.5, tz, trip);
  #endif

  vec4 diffuse = texture2D(s_MatTexture, uv);
  vec4 color = v_color0;

  #ifdef ALPHA_TEST
    if (diffuse.a < 0.6) {
      discard;
    }
  #endif

  #if defined(SEASONS) && (defined(OPAQUE) || defined(ALPHA_TEST))
    diffuse.rgb *= mix(vec3(1.0,1.0,1.0), texture2D(s_SeasonsTexture, v_color1.xy).rgb * 2.0, v_color1.z);
  #endif

  vec3 glow = nlGlow(s_MatTexture, uv, v_extra.a);

  diffuse.rgb *= diffuse.rgb;

  #if defined(TRANSPARENT) && !(defined(SEASONS) || defined(RENDER_AS_BILLBOARDS))
    if (v_extra.b > 0.9) {
      diffuse.rgb = vec3_splat(1.0 - NL_WATER_TEX_OPACITY*(1.0 - diffuse.b*1.8));
      diffuse.a = color.a;
    }
  #else
    diffuse.a = 1.0;
  #endif

  diffuse.rgb *= color.rgb;
  diffuse.rgb += glow;

  if (v_extra.b > 0.9) {
    diffuse.rgb += v_refl.rgb*v_refl.a;
  } else if (v_refl.a > 0.0) {
    // reflective effect - only on xz plane
    float dy = abs(dFdy(v_extra.g));
    if (dy < 0.0002) {
      float mask = v_refl.a*(clamp(v_extra.r*10.0,8.2,8.8)-7.8);
      diffuse.rgb *= 1.0 - 0.6*mask;
      diffuse.rgb += v_refl.rgb*mask;
    }
  }

  diffuse.rgb = mix(diffuse.rgb, v_fog.rgb, v_fog.a);

  #ifdef NL_TRIP
    // Seelenreich: Kanten und Lichthöfe auf dem linearen Texel
    diffuse.rgb = tripKanten(diffuse.rgb, trip);
    diffuse.rgb = tripHalo(s_MatTexture, uv, diffuse.rgb, trip);
    #ifdef NL_TRIP_TIEFE
      // Stufe 2 bis 4: Farbwandel je Block, Geometrie, Rekursion, Tunnel im Dunkeln
      vec3 rel = v_trip.xyz - CameraPosition.xyz;
      vec3 richtung = rel/max(length(rel), 0.001);
      float dunkel = 1.0 - smoothstep(0.05, 0.45, max(v_lightmapUV.x, v_lightmapUV.y));
      diffuse.rgb = tripFarbwandel(diffuse.rgb, v_trip.xyz, tz, trip);
      diffuse.rgb = tripGeometrie(diffuse.rgb, v_trip.xyz, tz, trip);
      diffuse.rgb = tripRekursion(diffuse.rgb, richtung, tz, trip);
      diffuse.rgb = tripTunnel(diffuse.rgb, rel, dunkel, tz, trip);
    #endif
  #endif

  diffuse.rgb = colorCorrection(diffuse.rgb);

  #ifdef NL_TRIP
    diffuse.rgb = tripFarbe(diffuse.rgb, trip);
  #endif

  gl_FragColor = diffuse;
}
