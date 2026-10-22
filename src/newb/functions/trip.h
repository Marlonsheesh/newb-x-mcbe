#ifndef TRIP_H
#define TRIP_H

// Seelenreich-Trip: Steuerkanal und Trip-Effekte.
// Vertrag: STEUERKANAL.md im Projekt MC-Psychodelics (Stand 22.10.2026).
//
// Der Shader kennt den Spielzustand nicht. Stufe und Phase kommen aus den Nebelwerten,
// die das Script per fogSettings setzt:
//   FogColor.b               -> Stufe s = 1..5 als Byte 77 + 3*s (80, 83, 86, 89, 92)
//   FogAndDistanceControl.y  -> Phase p = 1..5 als fog_end 0,90 + 0,01*p (0,91 … 0,95)
// Ist eine der beiden Kennungen ungültig, gilt kein Trip (Stufe 0, Phase 0). Dann ist
// jeder Effekt exakt 0 und Newb rendert unverändert.
//
// Die Funktionen sind nur mit NL_TRIP (config.h) aktiv.

#include "utils.h"

#ifdef NL_TRIP

// Codierung (STEUERKANAL.md, Abschnitt Codierung)
#define TRIP_BLAU_BASIS   77.0  // Blaukanal in Byte bei Stufe 0
#define TRIP_BLAU_SCHRITT 3.0   // je Stufe 3 Byte
#define TRIP_ENDE_BASIS   0.90  // fog_end bei Phase 0
#define TRIP_ENDE_SCHRITT 100.0 // je Phase 0,01 fog_end

// Einsatzstufen je Effekt (PLAN_phase1.md, Abschnitt 2): alle ab Stufe 1 in Phase 1
#define TRIP_EINSATZ_FARBE  1.0
#define TRIP_EINSATZ_KANTEN 1.0
#define TRIP_EINSATZ_ATEM   1.0
#define TRIP_EINSATZ_HALO   1.0

// Stärken bei Effektgewicht 1,0
#define TRIP_FARBE_SAETTIGUNG 0.5   // Sättigung +50 %
#define TRIP_FARBE_KONTRAST   0.12  // leichter Kontrast
#define TRIP_KANTEN_VERSTAERKUNG 3.0 // Verstärkung der Helligkeitsableitung
#define TRIP_KANTEN_STAERKE   0.5   // Anteil des warmen Saums
#define TRIP_HALO_SCHWELLE    0.5   // ab dieser (linearen) Helligkeit strahlt ein Texel
#define TRIP_HALO_STAERKE     0.8   // Anteil des Lichthofs

struct TripState {
  float stufe;       // 0 = kein Trip, sonst 1..5
  float phase;       // 0 = kein Trip, sonst 1 einstieg, 2 aufstieg, 3 hoehepunkt, 4 blende, 5 ausklang
  float intensitaet; // stufe / 5 (Stufe 5 ist der Deckel)
  float gewicht;     // intensitaet * Phasengewicht; 0 ohne Trip
};

// Stufe aus dem Blaukanal (Byte). Gibt 0 zurück, wenn der Wert nicht im Vertrag liegt.
float tripStufe(vec3 fogColor) {
  float b = fogColor.b*255.0;
  float s = floor((b - TRIP_BLAU_BASIS)/TRIP_BLAU_SCHRITT + 0.5);
  float rest = abs(b - (TRIP_BLAU_BASIS + TRIP_BLAU_SCHRITT*s));
  return (s >= 1.0 && s <= 5.0 && rest <= 1.0) ? s : 0.0;
}

// Phase aus fog_end (relativ zur Sichtweite). Gibt 0 zurück, wenn ungültig.
float tripPhase(float fogEnde) {
  float x = (fogEnde - TRIP_ENDE_BASIS)*TRIP_ENDE_SCHRITT;
  float p = floor(x + 0.5);
  float rest = abs(x - p);
  return (p >= 1.0 && p <= 5.0 && rest <= 0.003) ? p : 0.0;
}

// Phasengewicht nach STEUERKANAL.md: einstieg 0,35 · aufstieg 0,70 · hoehepunkt 1,00 ·
// blende 1,00 · ausklang 0,40. Ohne Phase 0.
float tripPhasenGewicht(float phase) {
  if (phase < 0.5) { return 0.0; }
  if (phase < 1.5) { return 0.35; }
  if (phase < 2.5) { return 0.70; }
  if (phase < 4.5) { return 1.00; }
  return 0.40;
}

TripState tripDecode(vec3 fogColor, vec2 fogControl) {
  TripState ts;
  float s = tripStufe(fogColor);
  float p = tripPhase(fogControl.y);
  // Ist eine der beiden Kennungen ungültig, gilt kein Trip (Stufe 0, Phase 0)
  if (s < 0.5 || p < 0.5) {
    s = 0.0;
    p = 0.0;
  }
  ts.stufe = s;
  ts.phase = p;
  ts.intensitaet = s/5.0;
  ts.gewicht = ts.intensitaet*tripPhasenGewicht(p);
  return ts;
}

// Effektstärke: das Gewicht, wenn die Einsatzstufe erreicht ist, sonst 0.
float tripEinsatz(TripState ts, float einsatz) {
  return ts.gewicht*step(einsatz, ts.stufe);
}

// Farbverstärkung (Himmel und Terrain): Sättigung, leichter Kontrast, warme Tönung Gold/Orange.
// Wirkt auf die Anzeigefarbe, also nach der Tonkorrektur.
vec3 tripFarbe(vec3 col, TripState ts) {
  float e = tripEinsatz(ts, TRIP_EINSATZ_FARBE);
  if (e <= 0.0) { return col; }
  col = mix(vec3_splat(luminance(col)), col, 1.0 + TRIP_FARBE_SAETTIGUNG*e);
  col = (col - 0.5)*(1.0 + TRIP_FARBE_KONTRAST*e) + 0.5;
  col *= mix(vec3(1.0, 1.0, 1.0), vec3(1.08, 0.96, 0.80), e);
  return max(col, vec3_splat(0.0));
}

// Kantenglühen: die Ableitung der Helligkeit (dFdx/dFdy) wird zu einem warmen Saum.
// Wirkt auf das lineare Texel, vor der Tonkorrektur.
vec3 tripKanten(vec3 col, TripState ts) {
  float e = tripEinsatz(ts, TRIP_EINSATZ_KANTEN);
  if (e <= 0.0) { return col; }
  float lum = luminance(col);
  #if BGFX_SHADER_TYPE_FRAGMENT
    // Ableitungen gibt es nur im Fragment-Shader; der Vertex-Compiler prüft die Funktion trotzdem
    float kante = clamp((abs(dFdx(lum)) + abs(dFdy(lum)))*TRIP_KANTEN_VERSTAERKUNG, 0.0, 1.0);
  #else
    float kante = 0.0;
  #endif
  return col + kante*e*TRIP_KANTEN_STAERKE*vec3(1.0, 0.72, 0.35);
}

// Lichthöfe: helle Nachbartexel strahlen als weicher Saum nach außen.
// Vier Nachbarn im Abstand von zwei Texeln, linear quadriert wie in RenderChunk.
vec3 tripHelleTexel(vec3 texel) {
  vec3 lin = texel*texel;
  return max(lin - vec3_splat(TRIP_HALO_SCHWELLE), vec3_splat(0.0));
}

vec3 tripHalo(sampler2D tex, vec2 uv, vec3 col, TripState ts) {
  float e = tripEinsatz(ts, TRIP_EINSATZ_HALO);
  if (e <= 0.0) { return col; }
  vec2 px = 2.0/vec2(textureSize(tex, 0));
  vec3 hell = tripHelleTexel(texture2DLod(tex, uv + vec2(px.x, 0.0), 0.0).rgb)
            + tripHelleTexel(texture2DLod(tex, uv - vec2(px.x, 0.0), 0.0).rgb)
            + tripHelleTexel(texture2DLod(tex, uv + vec2(0.0, px.y), 0.0).rgb)
            + tripHelleTexel(texture2DLod(tex, uv - vec2(0.0, px.y), 0.0).rgb);
  return col + 0.25*hell*e*TRIP_HALO_STAERKE*vec3(1.0, 0.85, 0.6);
}

// Atmen: radiale, langsam pulsierende Verschiebung der Geometrie um den Spieler (nur Vertex).
// Amplitude je Stufe von NL_TRIP_ATEM_AMP_MIN bis NL_TRIP_ATEM_AMP_MAX Blöcke, mal Phasengewicht.
// Periode NL_TRIP_ATEM_PERIOD Sekunden. Nahe Geometrie (unter 1 Block) bleibt fest.
// t ist ViewPositionAndTime.w in Sekunden, zentrum ist ViewPositionAndTime.xyz (wie in Newb).
vec3 tripAtmen(vec3 worldPos, vec3 zentrum, float t, TripState ts) {
  float e = tripEinsatz(ts, TRIP_EINSATZ_ATEM);
  if (e <= 0.0) { return worldPos; }
  vec3 rel = worldPos - zentrum;
  float r = length(rel);
  vec3 richtung = rel/max(r, 0.001);
  float intens = mix(NL_TRIP_ATEM_AMP_MIN, NL_TRIP_ATEM_AMP_MAX, ts.intensitaet);
  float amp = intens*tripPhasenGewicht(ts.phase);
  float puls = sin(t*(2.0*PI/NL_TRIP_ATEM_PERIOD));
  float nah = smoothstep(1.0, 8.0, r);
  return worldPos + richtung*(amp*puls*nah);
}

// ---------------------------------------------------------------------------
// Platzhalter für Phase C3 (Stufe 2 bis 4 und Himmels-Mandala).
// Jede Funktion gibt die Eingabe unverändert zurück, bis C3 sie ausfüllt.
// Der Shader-Compiler entfernt ungenutzte Funktionen.
// ---------------------------------------------------------------------------

// Phase C3: Fließen und Schmelzen (Stufe 2). Platzhalter.
vec2 tripFliessen(vec2 uv, TripState ts) {
  return uv;
}

// Phase C3: Farbwandel pro Oberfläche (Stufe 2). Platzhalter.
vec3 tripFarbwandel(vec3 col, vec3 worldPos, TripState ts) {
  return col;
}

// Phase C3: Geometrie auf Oberflächen, Waben, Gitter, Spiralen (Stufe 2). Platzhalter.
vec3 tripGeometrie(vec3 col, vec3 worldPos, TripState ts) {
  return col;
}

// Phase C3: Raum dehnt und staucht sich (Stufe 2). Platzhalter.
vec3 tripRaum(vec3 worldPos, vec3 zentrum, TripState ts) {
  return worldPos;
}

// Phase C3: Rekursion, log-polares Muster, Fraktal-Zoom (Stufe 3). Platzhalter.
vec2 tripRekursion(vec2 uv, TripState ts) {
  return uv;
}

// Phase C3: Tunnel im Dunkeln (Stufe 4), in der Blende immer. Platzhalter.
vec3 tripTunnel(vec3 col, TripState ts) {
  return col;
}

// Phase C3: Himmels-Mandala. Stufe noch festzulegen. Platzhalter.
vec3 tripHimmelsMandala(vec3 col, vec3 viewDir, TripState ts) {
  return col;
}

#endif // NL_TRIP

#endif
