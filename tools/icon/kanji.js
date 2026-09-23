const fs=require("fs"), zlib=require("zlib");
const {loadPNG}=require(__dirname+"/png.js");
const T=loadPNG("data/data/menu/homeAtlas.png");

// where the two characters live: scan the lower-left quarter for ink
function bbox(x0,y0,x1,y1,thr){
  let mnx=1e9,mny=1e9,mxx=-1,mxy=-1;
  for(let y=y0;y<y1;y++) for(let x=x0;x<x1;x++){
    const a=T.data[(y*T.w+x)*T.ch+3];
    if(a>thr){ if(x<mnx)mnx=x; if(x>mxx)mxx=x; if(y<mny)mny=y; if(y>mxy)mxy=y; }
  }
  return [mnx,mny,mxx,mxy];
}
const B=bbox(0, 760, 667, 2047, 40);   // 667: the gap before a neighbouring glyph
console.log("kanji box", B.join(" "), "=", (B[2]-B[0]+1)+"x"+(B[3]-B[1]+1));

function crc32(b){ let TB=crc32.t; if(!TB){TB=crc32.t=[];for(let n=0;n<256;n++){let c=n;for(let k=0;k<8;k++)c=c&1?0xEDB88320^(c>>>1):c>>>1;TB[n]=c>>>0;}}
  let c=0xFFFFFFFF; for(const x of b) c=TB[(c^x)&255]^(c>>>8); return (c^0xFFFFFFFF)>>>0; }
function writePNG(path,S,img){
  const ck=(ty,d)=>{const l=Buffer.alloc(4);l.writeUInt32BE(d.length);const td=Buffer.concat([Buffer.from(ty,"latin1"),d]);const c=Buffer.alloc(4);c.writeUInt32BE(crc32(td)>>>0);return Buffer.concat([l,td,c]);};
  const stride=S*4, raw=Buffer.alloc((stride+1)*S);
  for(let y=0;y<S;y++){raw[y*(stride+1)]=0;img.copy(raw,y*(stride+1)+1,y*stride,(y+1)*stride);}
  const ih=Buffer.alloc(13);ih.writeUInt32BE(S,0);ih.writeUInt32BE(S,4);ih[8]=8;ih[9]=6;
  fs.writeFileSync(path,Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),ck("IHDR",ih),ck("IDAT",zlib.deflateSync(raw)),ck("IEND",Buffer.alloc(0))]));
}
// bilinear lift of the ink, composited over a flat ground
function make(path, S, ink, ground, fill){
  const [x0,y0,x1,y1]=B, bw=x1-x0+1, bh=y1-y0+1;
  const k = (S*fill)/Math.max(bw,bh);
  const dw=bw*k, dh=bh*k, ox=(S-dw)/2, oy=(S-dh)/2;
  const img=Buffer.alloc(S*S*4);
  for(let i=0;i<S*S;i++){ img[i*4]=ground[0]; img[i*4+1]=ground[1]; img[i*4+2]=ground[2]; img[i*4+3]=255; }
  for(let y=0;y<S;y++) for(let x=0;x<S;x++){
    const u=(x+0.5-ox)/k + x0, v=(y+0.5-oy)/k + y0;
    if(u<x0||v<y0||u>x1||v>y1) continue;
    const iu=Math.floor(u), iv=Math.floor(v), fu=u-iu, fv=v-iv;
    let a=0;
    for(const [dx,dy,w] of [[0,0,(1-fu)*(1-fv)],[1,0,fu*(1-fv)],[0,1,(1-fu)*fv],[1,1,fu*fv]]){
      const xx=Math.min(T.w-1,iu+dx), yy=Math.min(T.h-1,iv+dy);
      a += T.data[(yy*T.w+xx)*T.ch+3]*w;
    }
    a/=255;
    a = Math.max(0, Math.min(1, (a - 0.22) / 0.46));   // firm up the edge
    const o=(y*S+x)*4;
    for(let c=0;c<3;c++) img[o+c]=Math.round(ink[c]*a + ground[c]*(1-a));
  }
  writePNG(path,S,img);
}
const OUT=process.env.SC+"/icon/out";
make(OUT+"/k1-kanji-red.png",   512, [245,244,248], [168,22,36], 0.78);
make(OUT+"/k4-kanji-red-1024.png", 1024, [245,244,248], [168,22,36], 0.78);
make(OUT+"/k5-kanji-dark-red.png", 512, [232,228,232], [124,14,26], 0.78);
make(OUT+"/k6-kanji-60.png", 60, [245,244,248], [168,22,36], 0.78);
make(OUT+"/k2-kanji-white.png", 512, [20,21,30],    [255,255,255], 0.80);
make(OUT+"/k3-kanji-night.png", 512, [240,240,246], [12,13,21],  0.80);
make(OUT+"/k1-kanji-red-120.png", 120, [245,244,248], [168,22,36], 0.80);
console.log("three kanji comps");
