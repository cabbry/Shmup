const fs=require("fs");
const {build, svg, png} = require(__dirname + "/render.js");
const OUT = process.env.SC + "/icon/out";
fs.mkdirSync(OUT, {recursive:true});

const SHIP = {yaw:0, pitch:84, zoom:1.35, shift:[0,0.14], stylise:true,
              cull:true, amb:0.42, key:0.88, rim:0.25, pad:0.06,
              light:[-0.42, 0.72, 0.62]};
const NIGHT = "#0b0c14";

// backgrounds, as SVG and as a preview colour
const BG = {
  none:  {svg:"",                                                     rgb:null},
  night: {svg:`<rect width="100%" height="100%" fill="${NIGHT}"/>`,   rgb:[11,12,20]},
  glow:  {svg:`<defs><radialGradient id="g" cx="50%" cy="46%" r="62%">
    <stop offset="0%" stop-color="#2a3350"/><stop offset="55%" stop-color="#141826"/>
    <stop offset="100%" stop-color="#070810"/></radialGradient></defs>
  <rect width="100%" height="100%" fill="url(#g)"/>`,                 rgb:[20,24,38], radial:true},
  ember: {svg:`<defs><radialGradient id="e" cx="50%" cy="72%" r="58%">
    <stop offset="0%" stop-color="#3a1220"/><stop offset="60%" stop-color="#12101a"/>
    <stop offset="100%" stop-color="#08070c"/></radialGradient></defs>
  <rect width="100%" height="100%" fill="url(#e)"/>`,                 rgb:[28,18,28], radial:true},
};

const VARIANTS = [
  ["01-night-tight", {}, "night"],
  ["02-no-background", {}, "none"],
  ["03-glow", {}, "glow"],
  ["04-ember", {}, "ember"],
  ["05-night-whole-ship", {zoom:1.0, shift:[0,0.02], pitch:88}, "night"],
  ["06-no-background-whole-ship", {zoom:1.0, shift:[0,0.02], pitch:88}, "none"],
];

for (const [name, over, bgName] of VARIANTS){
  const bg = BG[bgName];
  const o = Object.assign({size:1024}, SHIP, over, {bg: bg.svg, rgb: bg.rgb});
  const tris = build(o);
  fs.writeFileSync(`${OUT}/${name}.svg`, svg(tris, o));
  const prev = Object.assign({}, o, {size:512});
  fs.writeFileSync(`${OUT}/${name}.png`, png(build(prev), prev));
  const kb = (fs.statSync(`${OUT}/${name}.svg`).size/1024).toFixed(0);
  console.log(`${name}: ${tris.length} triangles, ${kb} KB of svg`);
}
