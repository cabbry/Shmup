const fs=require("fs");
const {build, svg, png} = require(__dirname + "/render.js");
const OUT = process.env.SC + "/icon/out";
fs.mkdirSync(OUT, {recursive:true});

const COMMON = {stylise:true, cull:true, amb:0.44, key:0.9, rim:0.20, pad:0.05,
                light:[-0.42, 0.72, 0.62]};
// two ways of holding the ship
const TOP   = {yaw:0,   pitch:84, roll:0,   zoom:1.35, shift:[0, 0.14]};   // from above, nose up
const Q34L  = {yaw:240, pitch:45, roll:18,  zoom:1.10, shift:[0, 0.02]};   // three-quarter, nose left
const Q34R  = {yaw:60,  pitch:45, roll:-18, zoom:1.10, shift:[0, 0.02]};   // and its mirror

const NIGHT = "#0b0c14";
const BG = {
  none:  {svg:"", rgb:null},
  night: {svg:`<rect width="100%" height="100%" fill="${NIGHT}"/>`, rgb:[11,12,20]},
  glow:  {svg:`<defs><radialGradient id="g" cx="50%" cy="46%" r="62%">
    <stop offset="0%" stop-color="#2a3350"/><stop offset="55%" stop-color="#141826"/>
    <stop offset="100%" stop-color="#070810"/></radialGradient></defs>
  <rect width="100%" height="100%" fill="url(#g)"/>`, rgb:[20,24,38]},
  ember: {svg:`<defs><radialGradient id="e" cx="50%" cy="70%" r="58%">
    <stop offset="0%" stop-color="#3a1220"/><stop offset="60%" stop-color="#12101a"/>
    <stop offset="100%" stop-color="#08070c"/></radialGradient></defs>
  <rect width="100%" height="100%" fill="url(#e)"/>`, rgb:[28,18,28]},
};

const VARIANTS = [
  ["01-top-night",        TOP,  "night"],
  ["02-top-plain",        TOP,  "none"],
  ["03-top-glow",         TOP,  "glow"],
  ["04-34-left-night",    Q34L, "night"],
  ["05-34-left-plain",    Q34L, "none"],
  ["06-34-left-glow",     Q34L, "glow"],
  ["07-34-left-ember",    Q34L, "ember"],
  ["08-34-right-night",   Q34R, "night"],
  ["09-34-right-plain",   Q34R, "none"],
];

for (const [name, view, bgName] of VARIANTS){
  const bg = BG[bgName];
  const o = Object.assign({size:1024}, COMMON, view, {bg: bg.svg, rgb: bg.rgb});
  const tris = build(o);
  fs.writeFileSync(`${OUT}/${name}.svg`, svg(tris, o));
  const prev = Object.assign({}, o, {size:512});
  fs.writeFileSync(`${OUT}/${name}.png`, png(build(prev), prev));
  console.log(`${name}: ${tris.length} triangles, ${(fs.statSync(`${OUT}/${name}.svg`).size/1024).toFixed(0)} KB`);
}
// the real test: the tight crop at the size an iPhone draws it
for (const s of [180, 120, 60]){
  const o = Object.assign({size:s}, COMMON, Q34L, {rgb:[11,12,20]});
  fs.writeFileSync(`${OUT}/size_${s}.png`, png(build(o), o));
}
console.log("and the 180 / 120 / 60 checks");
