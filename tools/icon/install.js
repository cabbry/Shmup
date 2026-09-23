const fs=require("fs"), path=require("path");
const {render, writePNG} = require(__dirname+"/real.js");

// the chosen picture: three-quarter, night blue, framed so the hull fills the square
const VIEW  = {yaw:215, pitch:35, roll:14, zoom:1.12, shift:[-0.06,0], pad:0.03};
// shifted left: at the old framing the cannon and the right hull -- the
// front of the ship -- fell outside the square.
const LIGHT = {amb:0.5, key:1.25, fillK:0.4, rimK:0.35, specK:0.7};
const GROUND = [26,30,48];                 // the same navy as the render
const ss = s => s<=192 ? 4 : s<=512 ? 3 : 2;

function shot(size, opts){
  const o = Object.assign({size, ss:ss(size)}, VIEW, LIGHT, opts);
  return render(o);
}
function circle(r){                         // Android's round launcher icon
  const S=r.size, img=Buffer.from(r.img);
  const c=(S-1)/2, rad=S/2;
  for(let y=0;y<S;y++) for(let x=0;x<S;x++){
    const d=Math.hypot(x-c,y-c);
    const a = d<=rad-1 ? 255 : d>=rad ? 0 : Math.round((rad-d)*255);
    img[(y*S+x)*4+3] = Math.min(img[(y*S+x)*4+3], a);
  }
  return {img, size:S};
}
const mk = (p, r) => { fs.mkdirSync(path.dirname(p), {recursive:true}); writePNG(p, r.size, r.img); console.log("  " + p + "  " + r.size); };

console.log("iOS");
mk("ios/Shmup/Images.xcassets/AppIcon-Shmup.appiconset/AppIcon1024.png", shot(1024, {rgb:GROUND}));

console.log("Android launcher");
const DPI = {mdpi:48, hdpi:72, xhdpi:96, xxhdpi:144, xxxhdpi:192};
for(const [d,s] of Object.entries(DPI)){
  const r = shot(s, {rgb:GROUND});
  mk(`android/app/src/main/res/mipmap-${d}/ic_launcher.png`, r);
  mk(`android/app/src/main/res/mipmap-${d}/ic_launcher_round.png`, circle(shot(s, {rgb:GROUND})));
}
// the adaptive foreground is 108dp and only its middle 72dp is ever visible:
// the ship has to sit inside two thirds of the square, on transparency
console.log("Android adaptive foreground (safe zone 2/3)");
const FG = {mdpi:108, hdpi:162, xhdpi:216, xxhdpi:324, xxxhdpi:432};
for(const [d,s] of Object.entries(FG))
  mk(`android/app/src/main/res/mipmap-${d}/ic_launcher_foreground.png`, shot(s, {rgb:null, pad:0.19}));

console.log("Play Store");
mk("android/app/src/main/ic_launcher-playstore.png", shot(512, {rgb:GROUND}));

console.log("the old loose icons");
for(const s of [76,120,152,512]) mk(`icons/icon${s}.png`, shot(s, {rgb:GROUND}));
mk("icons/icon512_circle.png", circle(shot(512, {rgb:GROUND})));
for(const [p,s] of [["ios/shmup_iPhone_icon.png",180],["ios/shmup_iPad_icon.png",167]]) mk(p, shot(s,{rgb:GROUND}));
