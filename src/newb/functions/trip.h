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
// Die Funktionen sind nur mit NL_TRIP (config.h) aktiv. Die Effekte der Stufen 2 bis 4
// sind zusätzlich über NL_TRIP_TIEFE (config.h) getrennt abschaltbar.
//
// Stil: organisch (warm, langsam, schmelzend) bei Stufe 1 bis 3; ab Stufe 4 kristallin
// (kühl, scharf, schnell). Der Stilfaktor kristall = smoothstep(3,4,s) geht in jeden Effekt ein.

#include "utils.h"
#include "noise.h"

#ifdef NL_TRIP

// Codierung (STEUERKANAL.md, Abschnitt Codierung)
#define TRIP_BLAU_BASIS   77.0  // Blaukanal in Byte bei Stufe 0
#define TRIP_BLAU_SCHRITT 3.0   // je Stufe 3 Byte
#define TRIP_ENDE_BASIS   0.90  // fog_end bei Phase 0
#define TRIP_ENDE_SCHRITT 100.0 // je Phase 0,01 fog_end

// Einsatzstufen je Effekt (PLAN_phase1.md, Abschnitt 2)
#define TRIP_EINSATZ_FARBE  1.0
#define TRIP_EINSATZ_KANTEN 1.0
#define TRIP_EINSATZ_ATEM   1.0
#define TRIP_EINSATZ_HALO   1.0
#define TRIP_EINSATZ_FLIESSEN   2.0
#define TRIP_EINSATZ_FARBWANDEL 2.0
#define TRIP_EINSATZ_GEOMETRIE  2.0
#define TRIP_EINSATZ_RAUM       2.0
#define TRIP_EINSATZ_REKURSION  3.0
#define TRIP_EINSATZ_TUNNEL     4.0  // im Dunkeln
#define TRIP_EINSATZ_TUNNEL_BLENDE 1.0 // in der Blende immer, ab Stufe 1

// Stärken bei Effektgewicht 1,0
#define TRIP_FARBE_SAETTIGUNG 0.5   // Sättigung +50 %
#define TRIP_FARBE_KONTRAST   0.12  // leichter Kontrast
#define TRIP_KANTEN_VERSTAERKUNG 3.0 // Verstärkung der Helligkeitsableitung (organisch)
#define TRIP_KANTEN_VERSTAERKUNG_KRISTALL 4.5 // härtere Kanten (kristallin)
#define TRIP_KANTEN_STAERKE   0.5   // Anteil des warmen Saums
#define TRIP_HALO_SCHWELLE    0.5   // ab dieser (linearen) Helligkeit strahlt ein Texel
#define TRIP_HALO_STAERKE     0.8   // Anteil des Lichthofs

// Stufe 2 bis 4: Bereiche, zwischen denen die Intensität I = s/5 interpoliert
#define TRIP_FLIESS_VERSATZ_MIN 1.5  // Texel bei Stufe 2
#define TRIP_FLIESS_VERSATZ_MAX 4.0  // Texel bei Stufe 5
#define TRIP_LAUB_TROPFEN 0.12       // Kachelanteile je Sekunde, Laub tropft nach unten
#define TRIP_FARBWANDEL_MAX 1.047198 // 60 Grad in Radiant bei Intensität 1
#define TRIP_GEOM_GEW_MIN 0.08       // Gewicht der Linien bei Stufe 1
#define TRIP_GEOM_GEW_MAX 0.3        // Gewicht der Linien bei Stufe 5
#define TRIP_RAUM_K_MIN 0.01         // radiale Dehnung bei Stufe 1
#define TRIP_RAUM_K_MAX 0.04         // radiale Dehnung bei Stufe 5
#define TRIP_RAUM_PERIODE 11.0       // Sekunden für eine Dehnungswelle
#define TRIP_RAUM_LAMBDA 24.0        // Blöcke Abstand zwischen zwei Wellen
#define TRIP_REK_GEW_MIN 0.1         // Gewicht der Rekursion bei Stufe 1
#define TRIP_REK_GEW_MAX 0.35        // Gewicht der Rekursion bei Stufe 5

struct TripState {
  float stufe;       // 0 = kein Trip, sonst 1..5
  float phase;       // 0 = kein Trip, sonst 1 einstieg, 2 aufstieg, 3 hoehepunkt, 4 blende, 5 ausklang
  float intensitaet; // stufe / 5 (Stufe 5 ist der Deckel)
  float gewicht;     // intensitaet * Phasengewicht; 0 ohne Trip
  float kristall;    // Stilfaktor: smoothstep(3, 4, stufe); 0 organisch, 1 kristallin
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
  ts.kristall = smoothstep(3.0, 4.0, s);
  return ts;
}

// Effektstärke: das Gewicht, wenn die Einsatzstufe erreicht ist, sonst 0.
float tripEinsatz(TripState ts, float einsatz) {
  return ts.gewicht*step(einsatz, ts.stufe);
}

// Weicher Einsatz für die Stufen 2 bis 4: 0 unterhalb der Einsatzstufe, 1 ab ihr.
// Der Übergang läuft über die Stufe davor (smoothstep einsatz-1 … einsatz).
float tripEinsatzWeich(float stufe, float einsatz) {
  return smoothstep(einsatz - 1.0, einsatz, stufe);
}

// Pseudozufall je Block (aus floor der Weltposition)
float tripHash3(vec3 p) {
  highp float h = dot(p, vec3(12.9898, 78.233, 37.719));
  return fract(sin(h)*43758.5453);
}

// Abstand zur nächsten ganzzahligen Linie (0 auf der Linie)
float tripLinie(float x) {
  return min(fract(x), 1.0 - fract(x));
}

// Farbwinkel-Drehung um die Graustufenachse (Rodrigues um (1,1,1)); Ergebnis nie negativ.
vec3 tripFarbdrehung(vec3 col, float winkel) {
  float c = cos(winkel);
  float s = sin(winkel)*0.57735027;
  float d = (1.0 - c)/3.0;
  float r0 = dot(vec3(c + d, d - s, d + s), col);
  float r1 = dot(vec3(d + s, c + d, d - s), col);
  float r2 = dot(vec3(d - s, d + s, c + d), col);
  return max(vec3(r0, r1, r2), vec3_splat(0.0));
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
// Wirkt auf das lineare Texel, vor der Tonkorrektur. Kristallin: härtere Kanten.
vec3 tripKanten(vec3 col, TripState ts) {
  float e = tripEinsatz(ts, TRIP_EINSATZ_KANTEN);
  if (e <= 0.0) { return col; }
  float lum = luminance(col);
  #if BGFX_SHADER_TYPE_FRAGMENT
    // Ableitungen gibt es nur im Fragment-Shader; der Vertex-Compiler prüft die Funktion trotzdem
    float verstaerkung = mix(TRIP_KANTEN_VERSTAERKUNG, TRIP_KANTEN_VERSTAERKUNG_KRISTALL, ts.kristall);
    float kante = clamp((abs(dFdx(lum)) + abs(dFdy(lum)))*verstaerkung, 0.0, 1.0);
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

// Lichthof um die Sonne am Himmel (Stufe 1). blick zeigt wie in nlRenderSky vom Spieler
// zum Himmel, sonne ist env.sunDir. Gewichtet wie die anderen Stufe-1-Effekte.
vec3 tripHimmelsHalo(vec3 col, vec3 blick, vec3 sonne, TripState ts) {
  float e = tripEinsatz(ts, TRIP_EINSATZ_HALO);
  if (e <= 0.0) { return col; }
  float c = max(dot(blick, sonne), 0.0);
  float bluete = 0.9*pow(c, 14.0) + 0.15*pow(c, 3.0);
  return col + bluete*e*vec3(1.0, 0.85, 0.55);
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

#ifdef NL_TRIP_TIEFE

// ---------------------------------------------------------------------------
// Stufe 2 bis 4 (Phase C3). Jede Funktion gibt die Eingabe exakt zurück, wenn ihr
// Gewicht 0 ist (kein Trip, Stufe unterhalb des Einsatzes, Phase ohne Gewicht).
// ---------------------------------------------------------------------------

// Fließen und Schmelzen (Stufe 2 ff.): domain-warped Noise auf die Textur-UV. Die Kacheln
// des Atlas (64 breit, wie glow.h) bleiben erhalten: die Verschiebung wird am Kachelrand
// mit 1,5 Texel Abstand gehalten. Laub (laub, Kennung aus wave.h/isTree) tropft zusätzlich
// langsam nach unten, die Position läuft dafür innerhalb der Kachel um.
vec2 tripFliessen(sampler2D tex, vec2 uv, bool laub, float t, TripState ts) {
  float e = tripEinsatzWeich(ts.stufe, TRIP_EINSATZ_FLIESSEN)*tripPhasenGewicht(ts.phase);
  if (e <= 0.0) { return uv; }
  vec2 texGroesse = vec2(textureSize(tex, 0));
  vec2 kacheln = vec2(64.0, 64.0*texGroesse.y/texGroesse.x);
  vec2 zelle = floor(uv*kacheln);
  vec2 lokal = fract(uv*kacheln);
  vec2 randLokal = 1.5*kacheln/texGroesse;
  float kristall = ts.kristall;
  float zeit = t*(1.0 + 0.8*kristall);

  // Domain-Warp: zwei Ebenen Noise, Frequenz steigt im kristallinen Stil
  vec2 q = lokal*(3.0 + 3.0*kristall) + zelle*1.7;
  vec2 v = vec2(noise2D(q + zeit*vec2(0.11, 0.07)), noise2D(q + vec2(5.2, 1.3) + zeit*vec2(-0.09, 0.13)));
  vec2 w = 2.0*(v - 0.5);
  vec2 d2 = vec2(noise2D(q + w + vec2(1.7, 9.2)), noise2D(q + w + vec2(8.3, 2.8)));
  vec2 d = 2.0*(d2 - 0.5);

  float versatz = mix(TRIP_FLIESS_VERSATZ_MIN, TRIP_FLIESS_VERSATZ_MAX, clamp((ts.stufe - 2.0)/3.0, 0.0, 1.0));

  // Laub tropft: die Position innerhalb der Kachel wandert nach unten und umläuft die Kachel
  vec2 lok = lokal;
  if (laub) {
    lok = mix(lokal, fract(lokal - vec2(0.0, TRIP_LAUB_TROPFEN*zeit)), e);
    lok = clamp(lok, randLokal, vec2(1.0, 1.0) - randLokal);
  }

  vec2 basis = (zelle + lok)/kacheln;
  vec2 kachelMin = zelle/kacheln + 1.5/texGroesse;
  vec2 kachelMax = (zelle + 1.0)/kacheln - 1.5/texGroesse;
  return clamp(basis + d*versatz*e/texGroesse, kachelMin, kachelMax);
}

// Farbwandel pro Oberfläche (Stufe 2 ff.): Hue-Rotation je Block, Amplitude 0 … 60° mit der
// Intensität, Schwingung je Block verschieden. Der Himmel wird nicht gefärbt (nur Terrain).
vec3 tripFarbwandel(vec3 col, vec3 gPos, float t, TripState ts) {
  float e = tripEinsatzWeich(ts.stufe, TRIP_EINSATZ_FARBWANDEL)*tripPhasenGewicht(ts.phase);
  if (e <= 0.0) { return col; }
  float h = tripHash3(floor(gPos));
  float winkel = TRIP_FARBWANDEL_MAX*ts.intensitaet*e*sin(6.2831853*h + t*(0.35 + 0.4*ts.kristall));
  return tripFarbdrehung(col, winkel);
}

// Geometrie auf Oberflächen (Stufe 2 ff.): feine Linien in Weltkoordinaten, auf die Blockfläche
// projiziert (Normale aus den Ableitungen). Je Block eines von drei Mustern: Waben, Gitter, Spiralen.
// Organisch: weich und atmend in Gold; kristallin: harte Linien in Türkis.
vec3 tripGeometrie(vec3 col, vec3 gPos, float t, TripState ts) {
  float e = tripEinsatzWeich(ts.stufe, TRIP_EINSATZ_GEOMETRIE)*tripPhasenGewicht(ts.phase);
  if (e <= 0.0) { return col; }
  #if BGFX_SHADER_TYPE_FRAGMENT
    vec3 normale = cross(dFdx(gPos), dFdy(gPos));
    vec3 an = abs(normale);
    vec2 p = gPos.xy;
    if (an.y >= an.x && an.y >= an.z) {
      p = gPos.xz;
    } else if (an.x >= an.z) {
      p = gPos.zy;
    }
    float kristall = ts.kristall;
    float zeit = t*(1.0 + 0.8*kristall);
    float h = tripHash3(floor(gPos));
    float typ = floor(h*3.0);
    float l;
    if (typ < 0.5) {
      // Waben: drei Liniensysteme im Winkel von 60 Grad, atmen im organischen Stil
      vec2 q = p*3.0*(1.0 + 0.04*sin(zeit*0.6)*(1.0 - kristall));
      l = min(tripLinie(q.x), min(tripLinie(0.5*q.x + 0.866025*q.y), tripLinie(-0.5*q.x + 0.866025*q.y)));
    } else if (typ < 1.5) {
      // Gitter
      vec2 q = p*3.0;
      l = min(tripLinie(q.x), tripLinie(q.y));
    } else {
      // Spiralen: zwei Arme je Zelle, logarithmisch nach außen
      vec2 c = fract(p*2.0) - 0.5;
      float r = length(c) + 0.02;
      l = tripLinie(2.0*atan2(c.y, c.x)/6.2831853 + r*4.0);
    }
    float breite = mix(0.12*(1.0 + 0.3*sin(zeit*0.7)), 0.03, kristall);
    float maske = 1.0 - smoothstep(0.0, breite, l);
    float gew = mix(TRIP_GEOM_GEW_MIN, TRIP_GEOM_GEW_MAX, ts.intensitaet)*e;
    vec3 farbe = mix(vec3(1.0, 0.78, 0.42), vec3(0.45, 0.95, 0.9), kristall);
    return col + gew*maske*farbe;
  #else
    return col;
  #endif
}

// Raum dehnt und staucht sich (Stufe 2 ff., nur Vertex von Terrain und Actor): radial um den
// Spieler, r -> r*(1 + k*sin(omega*t - r/lambda)). Nicht am Himmel.
vec3 tripRaum(vec3 worldPos, vec3 zentrum, float t, TripState ts) {
  float e = tripEinsatzWeich(ts.stufe, TRIP_EINSATZ_RAUM)*tripPhasenGewicht(ts.phase);
  if (e <= 0.0) { return worldPos; }
  vec3 rel = worldPos - zentrum;
  float r = length(rel);
  float k = mix(TRIP_RAUM_K_MIN, TRIP_RAUM_K_MAX, ts.intensitaet)*e;
  float dehnung = k*sin(t*(2.0*PI/TRIP_RAUM_PERIODE) - r/TRIP_RAUM_LAMBDA);
  return zentrum + rel*(1.0 + dehnung);
}

// Rekursion, Fraktal-Zoom (Stufe 3 ff.): log-polares Muster über die Blickrichtung, das langsam
// nach innen zoomt. richtung ist die Blickrichtung vom Spieler zur Fläche (Einheitsvektor).
// Gewicht 0,1 … 0,35 mit der Intensität, additiv in violett/türkis (kristallin scharf).
vec3 tripRekursion(vec3 col, vec3 richtung, float t, TripState ts) {
  float e = tripEinsatzWeich(ts.stufe, TRIP_EINSATZ_REKURSION)*tripPhasenGewicht(ts.phase);
  if (e <= 0.0) { return col; }
  vec2 q = richtung.xz;
  float r = length(q) + 0.04;
  float phi = atan2(q.y, q.x + 0.00001);
  float zoom = t*(0.25 + 0.2*ts.kristall);
  // Phase = 9 log(r) + zoom: konstante Linien wandern zur Mitte, das Muster zoomt hinein
  float muster = 0.5 + 0.5*cos(6.0*phi + 9.0*log(r) + zoom);
  muster = mix(muster, step(0.5, muster), ts.kristall);
  float gew = mix(TRIP_REK_GEW_MIN, TRIP_REK_GEW_MAX, ts.intensitaet)*e;
  vec3 farbe = mix(vec3(1.0, 0.6, 0.3), vec3(0.6, 0.4, 1.0), ts.kristall);
  return col + gew*muster*farbe;
}

// Tunnel (Stufe 4 ff. im Dunkeln, in der Blende immer ab Stufe 1): ersetzt die Welt durch
// konzentrische Ringe, die auf den Spieler zulaufen. dunkel 0…1 aus dem Lichtwert der Fläche.
// Kristallin: höhere Ringdichte, härtere Ringe, Farbe Türkis statt Gold.
vec3 tripTunnel(vec3 col, vec3 rel, float dunkel, float t, TripState ts) {
  bool blende = abs(ts.phase - 4.0) < 0.5;
  float einsatz = blende ? tripEinsatzWeich(ts.stufe, TRIP_EINSATZ_TUNNEL_BLENDE) : tripEinsatzWeich(ts.stufe, TRIP_EINSATZ_TUNNEL);
  float w = einsatz*ts.intensitaet*tripPhasenGewicht(ts.phase)*(blende ? 1.0 : dunkel);
  if (w <= 0.0) { return col; }
  float d = length(rel);
  float zeit = t*(1.0 + 0.8*ts.kristall);
  float dichte = mix(6.0, 11.0, ts.kristall);
  float ring = fract(dichte/max(d, 0.5) - 0.35*zeit);
  float linie = 1.0 - smoothstep(0.0, mix(0.2, 0.06, ts.kristall), min(ring, 1.0 - ring));
  vec3 farbe = mix(vec3(1.0, 0.72, 0.3), vec3(0.3, 0.95, 0.9), ts.kristall);
  vec3 tunnelFarbe = farbe*(0.12 + 1.1*linie);
  return mix(col, tunnelFarbe, w);
}

// Himmels-Mandala: Stufe und Zählung (6-, 8- oder 12-zählig) offen, Rückfrage an Marlon. Platzhalter.
vec3 tripHimmelsMandala(vec3 col, vec3 viewDir, TripState ts) {
  return col;
}

#endif // NL_TRIP_TIEFE

#endif // NL_TRIP

#endif
