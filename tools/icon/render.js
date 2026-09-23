const fs=require("fs"), zlib=require("zlib");
const {loadMesh, loadPNG} = require(__dirname + "/mesh.js");

const M = loadMesh(process.env.MESH || "data/data/models/players/hpp_ship.obj.md5mesh");
const T = loadPNG(process.env.TEX  || "data/data/textures/object/hpp1.png");

function sample(u, v){                     // nearest, v flipped like the engine
  let x = Math.min(T.w-1, Math.max(0, Math.round(u*(T.w-1))));
  let y = Math.min(T.h-1, Math.max(0, Math.round((1-v)*(T.h-1))));
  const o = (y*T.w + x)*T.ch;
  return [T.data[o], T.data[o+1], T.data[o+2]];
}
const sub=(a,b)=>[a[0]-b[0],a[1]-b[1],a[2]-b[2]];
const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]];
const dot=(a,b)=>a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
function norm(a){ const l=Math.hypot(...a)||1; return [a[0]/l,a[1]/l,a[2]/l]; }
function rotY(p,a){ const c=Math.cos(a),s=Math.sin(a); return [c*p[0]+s*p[2], p[1], -s*p[0]+c*p[2]]; }
function rotX(p,a){ const c=Math.cos(a),s=Math.sin(a); return [p[0], c*p[1]-s*p[2], s*p[1]+c*p[2]]; }
function rotZ(p,a){ const c=Math.cos(a),s=Math.sin(a); return [c*p[0]-s*p[1], s*p[0]+c*p[1], p[2]]; }

// ---- build the shaded, projected triangles
function build(o){
  const yaw=o.yaw*Math.PI/180, pitch=o.pitch*Math.PI/180, roll=(o.roll||0)*Math.PI/180;
  const P = M.pos.map(p => rotZ(rotX(rotY(p, yaw), pitch), roll));
  let mn=[1e9,1e9,1e9], mx=[-1e9,-1e9,-1e9];
  for(const p of P) for(let i=0;i<3;i++){ if(p[i]<mn[i])mn[i]=p[i]; if(p[i]>mx[i])mx[i]=p[i]; }
  const S=o.size, pad=o.pad===undefined?0.04:o.pad;
  const w=mx[0]-mn[0], h=mx[1]-mn[1];
  const k = (S*(1-2*pad)) / Math.max(w,h);          // fill the square on the long side
  const cx=(mn[0]+mx[0])/2, cy=(mn[1]+mx[1])/2;
  const z2 = o.zoom || 1;                       // crop in on the big shapes
  const sx = (o.shift ? o.shift[0] : 0) * S;    // and slide the mass in frame
  const sy = (o.shift ? o.shift[1] : 0) * S;
  const px = p => [S/2 + (p[0]-cx)*k*z2 + sx, S/2 - (p[1]-cy)*k*z2 + sy];
  const L = norm(o.light||[-0.45, 0.75, 0.6]);
  const out=[];
  for(const t of M.tris){
    const a=P[t[0]], b=P[t[1]], c=P[t[2]];
    let n = norm(cross(sub(b,a), sub(c,a)));
    const facing = n[2];                              // +Z is toward the eye
    if (o.cull && facing <= 0) continue;
    if (facing < 0) n = [-n[0],-n[1],-n[2]];          // two-sided shading
    // one sample at the centroid misses a red marking that only crosses part
    // of the face: average the centroid and three inner points
    const U=[M.verts[t[0]].u,M.verts[t[1]].u,M.verts[t[2]].u];
    const V=[M.verts[t[0]].v,M.verts[t[1]].v,M.verts[t[2]].v];
    // nine samples, and the MEDIAN luminance decides the material: a mean lets
    // one bright speck flip a whole face to white, which is what made the
    // stylised hull sparkle like static
    const bary=[[1/3,1/3,1/3],[0.6,0.2,0.2],[0.2,0.6,0.2],[0.2,0.2,0.6],
                [0.45,0.45,0.1],[0.45,0.1,0.45],[0.1,0.45,0.45],
                [0.5,0.3,0.2],[0.2,0.3,0.5]];
    const S9=[];
    let alb=[0,0,0];   // eslint-disable-line prefer-const
    for(const w of bary){
      const c2 = sample(U[0]*w[0]+U[1]*w[1]+U[2]*w[2], V[0]*w[0]+V[1]*w[1]+V[2]*w[2]);
      S9.push(c2);
      alb[0]+=c2[0]/bary.length; alb[1]+=c2[1]/bary.length; alb[2]+=c2[2]/bary.length;
    }
    const lums = S9.map(c2=>(0.299*c2[0]+0.587*c2[1]+0.114*c2[2])/255).sort((a,b)=>a-b);
    const medianLum = lums[(lums.length-1)>>1];
    const redVotes = S9.filter(c2 => c2[0] > 1.35*c2[1] && c2[0] > 1.25*c2[2] &&
                                     Math.max(...c2)-Math.min(...c2) > 18).length;
    // Round 91: the hull texture is dark and busy; an icon needs matter you can
    // read at 60 pixels. Keep WHERE the texture puts its plates, its greebles
    // and its red, remap WHAT they are onto three tones and one accent.
    if (o.stylise){
      const PAL = o.palette || {plate:[236,235,240], steel:[150,152,166], dark:[44,46,60], accent:[176,32,46]};
      const red = redVotes >= 5;                 // a majority of the face, not a speck
      // a hard threshold makes neighbouring faces flip between two tones and
      // the hull turns into a checkerboard: ride the ramp instead
      const mix=(A,B,t)=>[0,1,2].map(i=>A[i]+(B[i]-A[i])*Math.max(0,Math.min(1,t)));
      alb = red ? PAL.accent
          : medianLum <= 0.34 ? mix(PAL.dark, PAL.steel, (medianLum-0.10)/0.24)
          : mix(PAL.steel, PAL.plate, (medianLum-0.34)/0.28);
    }
    const lam = Math.max(0, dot(n, L));
    const rim = Math.pow(1 - Math.abs(n[2]), 3) * (o.rim===undefined?0.35:o.rim);
    const amb = o.amb===undefined?0.30:o.amb;
    const g = amb + (o.key===undefined?0.95:o.key)*lam;
    const col = alb.map(v => Math.min(255, Math.round(v*g + 255*rim*0.35 + (o.lift||0))));
    out.push({pts:[px(a),px(b),px(c)], zs:[a[2],b[2],c[2]], z:(a[2]+b[2]+c[2])/3, col});
  }
  out.sort((p,q)=>p.z-q.z);                           // painter, far first
  return out;
}

// ---- SVG
function svg(tris, o){
  const S=o.size, L=[];
  L.push(`<svg xmlns="http://www.w3.org/2000/svg" width="${S}" height="${S}" viewBox="0 0 ${S} ${S}">`);
  if (o.bg) L.push(o.bg);
  for(const t of tris){
    const p = t.pts.map(q=>q[0].toFixed(1)+","+q[1].toFixed(1)).join(" ");
    L.push(`<polygon points="${p}" fill="rgb(${t.col[0]},${t.col[1]},${t.col[2]})"/>`);
  }
  L.push("</svg>");
  return L.join("\n");
}

// ---- a tiny rasterizer, so the eye can judge before anyone ships it
function raster(tris, o){
  const S=o.size, img=Buffer.alloc(S*S*4);
  const zb=new Float32Array(S*S).fill(-1e9);
  if(o.rgb){ for(let i=0;i<S*S;i++){ img[i*4]=o.rgb[0]; img[i*4+1]=o.rgb[1]; img[i*4+2]=o.rgb[2]; img[i*4+3]=255; } }
  for(const t of tris){
    const [A,B,C]=t.pts;
    const x0=Math.max(0,Math.floor(Math.min(A[0],B[0],C[0]))), x1=Math.min(S-1,Math.ceil(Math.max(A[0],B[0],C[0])));
    const y0=Math.max(0,Math.floor(Math.min(A[1],B[1],C[1]))), y1=Math.min(S-1,Math.ceil(Math.max(A[1],B[1],C[1])));
    const d=(B[1]-C[1])*(A[0]-C[0])+(C[0]-B[0])*(A[1]-C[1]);
    if(Math.abs(d)<1e-9) continue;
    for(let y=y0;y<=y1;y++) for(let x=x0;x<=x1;x++){
      const l1=((B[1]-C[1])*(x+0.5-C[0])+(C[0]-B[0])*(y+0.5-C[1]))/d;
      const l2=((C[1]-A[1])*(x+0.5-C[0])+(A[0]-C[0])*(y+0.5-C[1]))/d;
      const l3=1-l1-l2;
      if(l1<0||l2<0||l3<0) continue;
      const z = l1*t.zs[0] + l2*t.zs[1] + l3*t.zs[2];
      if(z <= zb[y*S+x]) continue;
      zb[y*S+x]=z;
      const o2=(y*S+x)*4;
      img[o2]=t.col[0]; img[o2+1]=t.col[1]; img[o2+2]=t.col[2]; img[o2+3]=255;
    }
  }
  return img;
}
function png(tris,o){
  const S=o.size, img=raster(tris,o);
  const stride=S*4, raw=Buffer.alloc((stride+1)*S);
  for(let y=0;y<S;y++){ raw[y*(stride+1)]=0; img.copy(raw, y*(stride+1)+1, y*stride, (y+1)*stride); }
  const chunk=(type,data)=>{
    const len=Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td=Buffer.concat([Buffer.from(type,"latin1"), data]);
    const crc=Buffer.alloc(4); crc.writeUInt32BE(crc32(td)>>>0);
    return Buffer.concat([len, td, crc]);
  };
  const ihdr=Buffer.alloc(13); ihdr.writeUInt32BE(S,0); ihdr.writeUInt32BE(S,4);
  ihdr[8]=8; ihdr[9]=6; ihdr[10]=0; ihdr[11]=0; ihdr[12]=0;
  return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),
                        chunk("IHDR",ihdr), chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]);
}
let TB=null;
function crc32(buf){
  if(!TB){ TB=[]; for(let n=0;n<256;n++){ let c=n; for(let k=0;k<8;k++) c = c&1 ? 0xEDB88320^(c>>>1) : c>>>1; TB[n]=c>>>0; } }
  let c=0xFFFFFFFF;
  for(const b of buf) c = TB[(c^b)&255] ^ (c>>>8);
  return (c^0xFFFFFFFF)>>>0;
}
module.exports={build, svg, png, raster};
