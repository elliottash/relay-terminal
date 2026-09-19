// SPDX-License-Identifier: GPL-3.0-or-later
// The page is exactly as tall as the part of the screen the reader can see.
//
// Owner, 2026-09-18, on an iPad: "it looks good in portrait but not landscape, it goes off screen"
// once the on-screen keyboard is up, and then "same on portrait actually". Safari does not shrink
// the layout viewport for its keyboard: `100%`, `100vh` and `innerHeight` all keep meaning the whole
// screen, the keyboard is laid over the bottom of it, and Safari scrolls the page to keep the
// focused box in view — which pushes the bar and the top of the pane off the screen, in either
// orientation. Landscape with a keyboard leaves about a third of the screen, portrait about 60%.
//
// The visual viewport is the part that is actually visible. Its height and its offset go into two
// custom properties on <html>; style.css pins the body to them, so the whole layout — bar, pane,
// composer — is sized to the space above the keyboard and nothing is left for Safari to scroll.
// Chrome on Android resizes the layout viewport itself when the viewport meta asks it to
// (`interactive-widget=resizes-content` in index.html); these properties then simply agree with it.

const root = document.documentElement;

// The visible height in CSS pixels, with the whole window as the answer where the browser has no
// visual viewport. Exported for the few measurements made in script (the prompt box's cap).
export function visibleHeight() {
  const vv = window.visualViewport;
  return vv ? vv.height : window.innerHeight;
}

let queued = false;
function apply() {
  queued = false;
  const vv = window.visualViewport;
  const height = Math.round(visibleHeight());
  // Pinch-zoom also shrinks the visual viewport, and the layout must not follow a zoom: only a
  // viewport at scale 1 is the keyboard talking.
  if (vv && Math.abs(vv.scale - 1) > 0.01) return;
  root.style.setProperty('--app-height', `${height}px`);
  root.style.setProperty('--app-top', `${Math.round(vv ? vv.offsetTop : 0)}px`);
  // Two steps: under 520 px (an iPad in landscape with its keyboard, about 330 left) the chrome
  // tightens; under 260 (a phone in landscape with its keyboard, about 185 left) only the terminal
  // and the prompt box are left at all. style.css and pane.css say what each step drops.
  root.classList.toggle('short-viewport', height < 520);
  root.classList.toggle('tiny-viewport', height < 260);
}

function schedule() {
  if (queued) return;
  queued = true;
  requestAnimationFrame(apply);
}

export function trackViewport() {
  apply();
  const vv = window.visualViewport;
  if (vv) {
    vv.addEventListener('resize', schedule);
    vv.addEventListener('scroll', schedule);
  }
  window.addEventListener('resize', schedule);
  window.addEventListener('orientationchange', schedule);
  // Safari still scrolls the window when a box near the bottom takes focus, even when the page
  // fits: put it back, the layout already keeps the box above the keyboard.
  window.addEventListener('scroll', () => {
    if (window.scrollY !== 0 && root.style.getPropertyValue('--app-height')) window.scrollTo(0, 0);
  }, { passive: true });
}
