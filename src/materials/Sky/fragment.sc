#ifndef INSTANCING
  $input v_worldPos, v_underwaterRainTimeDay
#endif

#include <bgfx_shader.sh>

#ifndef INSTANCING
  #include <newb/main.sh>
  uniform vec4 TimeOfDay;
  uniform vec4 FogColor;
  uniform vec4 FogAndDistanceControl;
#endif

void main() {
  #ifndef INSTANCING
    vec3 viewDir = normalize(v_worldPos);

    nl_environment env;
    env.end = false;
    env.nether = false;
    env.underwater = v_underwaterRainTimeDay.x > 0.5;
    env.rainFactor = v_underwaterRainTimeDay.y;
    env.dayFactor = v_underwaterRainTimeDay.w;
    env.fogCol = FogColor.rgb;
    env = calculateSunParams(env, TimeOfDay.x);

    nl_skycolor skycol = nlOverworldSkyColors(env);

    vec3 skyColor = nlRenderSky(skycol, env, -viewDir, v_underwaterRainTimeDay.z, true);
    #ifdef NL_SHOOTING_STAR
      skyColor += NL_SHOOTING_STAR*nlRenderShootingStar(viewDir, env.fogCol, v_underwaterRainTimeDay.z);
    #endif
    #ifdef NL_GALAXY_STARS
      skyColor += NL_GALAXY_STARS*nlRenderGalaxy(viewDir, env.fogCol, env, v_underwaterRainTimeDay.z);
    #endif

    #ifdef NL_TRIP
      // Seelenreich: Lichthof um die Sonne (Stufe 1), Rekursion am Himmel (Stufe 3 ff.)
      TripState trip = tripDecode(FogColor.rgb, FogAndDistanceControl.xy);
      skyColor = tripHimmelsHalo(skyColor, -viewDir, env.sunDir, trip);
      #ifdef NL_TRIP_TIEFE
        skyColor = tripRekursion(skyColor, -viewDir, v_underwaterRainTimeDay.z, trip);
      #endif
    #endif

    skyColor = colorCorrection(skyColor);

    #ifdef NL_TRIP
      // Farbverstärkung am Himmel (das Himmels-Mandala ist noch offen)
      skyColor = tripFarbe(skyColor, trip);
    #endif

    gl_FragColor = vec4(skyColor, 1.0);
  #else
    gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
  #endif
}
