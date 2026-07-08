const fs = require("fs");
const path = require("path");
const { chromium } = require("playwright");

const outDir = path.resolve(__dirname);
const htmlPath = path.join(outDir, "slides.html");
const slides = [
  ["slide-1", "01-crowpanel-hook.png"],
  ["slide-2", "02-what-it-is.png"],
  ["slide-3", "03-stack.png"],
  ["slide-4", "04-wifi-recovery.png"],
  ["slide-5", "05-screen-set.png"],
  ["slide-6", "06-lesson-cta.png"],
];

(async () => {
  fs.mkdirSync(outDir, { recursive: true });
  const chromePath = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
  const launchOptions = fs.existsSync(chromePath) ? { executablePath: chromePath } : {};
  const browser = await chromium.launch(launchOptions);
  const page = await browser.newPage({ viewport: { width: 1088, height: 1088 }, deviceScaleFactor: 1 });
  await page.goto(`file://${htmlPath}`);
  await page.waitForLoadState("networkidle");
  for (const [id, filename] of slides) {
    await page.locator(`#${id}`).screenshot({ path: path.join(outDir, filename) });
  }
  await browser.close();
  console.log(`Exported ${slides.length} slides to ${outDir}`);
})();
