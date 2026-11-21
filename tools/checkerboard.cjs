// checkerboard.cjs - printable checkerboard for the camera grid calibration (CAL TOP GRID / BOTTOM GRID)
//   node tools/checkerboard.cjs              -> hardware/calibration/checkerboard-{1,1.5,2,3,5,10}mm.svg
//   node tools/checkerboard.cjs 2.5 [out.svg] -> one board with 2.5 mm squares
// A4 page in real mm: print at 100 % (no "fit to page"), then check the 50 mm bar with a ruler.
// The same drawing is used by web/public/checkerboard.html (keep both in step).
const fs = require('fs');
const path = require('path');

function checkerboardSvg(size) {
  const pageW = 210, pageH = 297, area = 150; // board about 150 x 150 mm
  const n = Math.max(4, Math.floor(area / size));
  const board = n * size, x0 = (pageW - board) / 2, y0 = 40;
  const r = (v) => +v.toFixed(4);
  let sq = '';
  for (let j = 0; j < n; j++)
    for (let i = 0; i < n; i++)
      if ((i + j) % 2 === 0) sq += `M${r(x0 + i * size)} ${r(y0 + j * size)}h${r(size)}v${r(size)}h${r(-size)}z`;
  const by = y0 + board + 15; // 50 mm check bar
  let ticks = '';
  for (let t = 0; t <= 50; t += 10) ticks += `<line x1="${30 + t}" y1="${by - 2}" x2="${30 + t}" y2="${by + 6}" stroke="#000" stroke-width="0.3"/>`;
  return `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="${pageW}mm" height="${pageH}mm" viewBox="0 0 ${pageW} ${pageH}">
<rect width="${pageW}" height="${pageH}" fill="#fff"/>
<text x="${pageW / 2}" y="16" font-family="Arial, sans-serif" font-size="6" text-anchor="middle">Camera calibration checkerboard - ${size} mm squares</text>
<text x="${pageW / 2}" y="25" font-family="Arial, sans-serif" font-size="3.6" text-anchor="middle">Print at 100 % (no fit to page), matte paper. Measure the bar below: it must be 50 mm.</text>
<text x="${pageW / 2}" y="31" font-family="Arial, sans-serif" font-size="3.6" text-anchor="middle">Setting cal_grid = ${size}  ·  Calibration &gt; Top / Bottom camera &gt; Checkerboard</text>
<path d="${sq}" fill="#000"/>
<rect x="${r(x0)}" y="${y0}" width="${r(board)}" height="${r(board)}" fill="none" stroke="#000" stroke-width="0.1"/>
<line x1="30" y1="${by + 2}" x2="80" y2="${by + 2}" stroke="#000" stroke-width="0.6"/>${ticks}
<text x="85" y="${by + 4}" font-family="Arial, sans-serif" font-size="4">50 mm</text>
</svg>
`;
}

module.exports = { checkerboardSvg };

if (require.main === module) {
  const arg = parseFloat(process.argv[2]);
  if (arg > 0) {
    const out = process.argv[3] || `checkerboard-${arg}mm.svg`;
    fs.writeFileSync(out, checkerboardSvg(arg));
    console.log('wrote', out);
  } else {
    const dir = path.join(__dirname, '..', 'hardware', 'calibration');
    fs.mkdirSync(dir, { recursive: true });
    for (const s of [1, 1.5, 2, 3, 5, 10]) {
      const out = path.join(dir, `checkerboard-${s}mm.svg`);
      fs.writeFileSync(out, checkerboardSvg(s));
      console.log('wrote', out);
    }
  }
}
