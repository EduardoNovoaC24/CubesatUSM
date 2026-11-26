const theme = document.getElementById('theme');
const stars = document.getElementById('stars');
const tabs = [...document.querySelectorAll('.tab-btn')];
const motion = matchMedia('(prefers-reduced-motion: reduce)');
let starFrame;

function canvasContext(canvas) {
  const { width, height } = canvas.getBoundingClientRect();
  const scale = Math.min(devicePixelRatio || 1, 2);
  canvas.width = Math.round(width * scale);
  canvas.height = Math.round(height * scale);
  const context = canvas.getContext('2d');
  if (context) context.scale(scale, scale);
  return { context, width, height };
}

function palette() {
  const style = getComputedStyle(document.documentElement);
  return [style.getPropertyValue('--accent').trim(), style.getPropertyValue('--starlight').trim()];
}

const svgNamespace = 'http://www.w3.org/2000/svg';
let nextMaskId = 0;

function projectedCorners(element, origin) {
  const markers = [[0, 0], [1, 0], [1, 1], [0, 1]].map(([x, y]) => {
    const marker = document.createElement('span');
    marker.style.cssText = `position:absolute;width:0;height:0;left:${x * 100}%;top:${y * 100}%;pointer-events:none`;
    element.append(marker);
    return marker;
  });
  const corners = markers.map(marker => {
    const rect = marker.getBoundingClientRect();
    return [rect.left - origin.left, rect.top - origin.top];
  });
  markers.forEach(marker => marker.remove());
  return corners;
}

function figureShapes(scene, origin) {
  if (scene.matches('.model-scene')) {
    return [...scene.querySelectorAll('.sat-face')].map(face => projectedCorners(face, origin))
      .filter(([[ax, ay], [bx, by], , [dx, dy]]) => (bx - ax) * (dy - ay) - (by - ay) * (dx - ax) > 0);
  }
  const rotated = [...scene.querySelectorAll('.mission-tripod i')]
    .map(leg => projectedCorners(leg, origin));
  const solid = [...scene.querySelectorAll('.mission-tripod b, .mission-tether, .mission-bus')].map(element => {
    const { left, top, right, bottom } = element.getBoundingClientRect();
    return [[left, top], [right + 11, top], [right + 11, bottom], [left, bottom]]
      .map(([x, y]) => [x - origin.left, y - origin.top]);
  });
  return [...rotated, ...solid];
}

function isInside(x, y, polygon) {
  let inside = false;
  for (let i = 0, j = polygon.length - 1; i < polygon.length; j = i++) {
    const [xi, yi] = polygon[i];
    const [xj, yj] = polygon[j];
    if ((yi > y) !== (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) inside = !inside;
  }
  return inside;
}

function createRevealMask(layer) {
  const svg = document.createElementNS(svgNamespace, 'svg');
  const mask = document.createElementNS(svgNamespace, 'mask');
  const path = document.createElementNS(svgNamespace, 'path');
  mask.id = 'station-assembly-' + ++nextMaskId;
  mask.setAttribute('maskUnits', 'objectBoundingBox');
  mask.setAttribute('maskContentUnits', 'userSpaceOnUse');
  mask.setAttribute('x', '0');
  mask.setAttribute('y', '0');
  mask.setAttribute('width', '1');
  mask.setAttribute('height', '1');
  path.setAttribute('fill', 'white');
  mask.append(path);
  svg.append(mask);
  svg.classList.add('assembly-mask');
  svg.setAttribute('aria-hidden', 'true');
  document.body.append(svg);
  layer.style.maskImage = `url("#${mask.id}")`;
  return {
    reveal(pathData) { path.setAttribute('d', pathData); },
    remove() {
      layer.style.removeProperty('mask-image');
      svg.remove();
    },
  };
}

function assembleModel(scene) {
  if (scene.querySelector('.model-particles')) return;
  let layer = scene.querySelector('.assembly-figure');
  if (!layer) {
    layer = document.createElement('div');
    layer.className = 'assembly-figure';
    layer.append(...scene.children);
    scene.append(layer);
  }

  const canvas = document.createElement('canvas');
  canvas.className = 'model-particles';
  canvas.setAttribute('aria-hidden', 'true');
  scene.append(canvas);
  const { context, width, height } = canvasContext(canvas);
  if (!context || !width || !height) {
    canvas.remove();
    return;
  }
  const shapes = figureShapes(scene, canvas.getBoundingClientRect());
  const cell = 12;
  const particles = [];
  for (let top = 0; top < height; top += cell) {
    for (let left = 0; left < width; left += cell) {
      const x = left + cell / 2;
      const y = top + cell / 2;
      const visible = shapes.some(shape => isInside(x, y, shape)) ||
        shapes.some(shape => [-5, 5].some(offset => isInside(x + offset, y, shape)));
      particles.push({
        x, y, left, top, visible,
        sx: Math.random() * width, sy: Math.random() * height,
        arrive: .38 + Math.random() * .58,
        size: .7 + Math.random() * 1.35,
      });
    }
  }
  const mask = createRevealMask(layer);
  const colors = palette();
  const start = performance.now();
  function frame(now) {
    const time = Math.min(1, (now - start) / 1500);
    if (time === 1 || motion.matches || !scene.clientWidth) {
      mask.remove();
      canvas.remove();
      return;
    }
    context.clearRect(0, 0, width, height);
    let revealed = '';
    particles.forEach((particle, i) => {
      if (time >= particle.arrive) {
        const settled = Math.min(1, (time - particle.arrive) / .035);
        const radius = .7 + (cell / 2 + .3) * settled;
        if (settled === 1) {
          revealed += `M${particle.left},${particle.top}h${cell}v${cell}h-${cell}Z`;
        } else {
          const r = radius.toFixed(2);
          revealed += `M${particle.x - radius},${particle.y}a${r},${r} 0 1,1 ${2 * radius},0a${r},${r} 0 1,1 ${-2 * radius},0Z`;
        }
        return;
      }
      if (!particle.visible) return;
      const progress = time / particle.arrive;
      const eased = progress * progress * (3 - 2 * progress);
      const x = particle.sx + (particle.x - particle.sx) * eased;
      const y = particle.sy + (particle.y - particle.sy) * eased;
      context.globalAlpha = .35 + .62 * progress;
      context.fillStyle = colors[i % colors.length];
      context.beginPath();
      context.arc(x, y, particle.size, 0, Math.PI * 2);
      context.fill();
    });
    mask.reveal(revealed);
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
}

function animateStars() {
  cancelAnimationFrame(starFrame);
  const { context, width, height } = canvasContext(stars);
  if (!context || motion.matches) return;
  const colors = palette();
  const dots = Array.from({ length: 85 }, () => {
    const angle = Math.random() * Math.PI * 2;
    return { x: Math.cos(angle), y: Math.sin(angle), distance: 20 + Math.random() * Math.max(width, height) * .65 };
  });
  const start = performance.now();
  function frame(now) {
    const time = Math.min(1, (now - start) / 520);
    context.clearRect(0, 0, width, height);
    context.globalAlpha = Math.sin(time * Math.PI) * .65;
    dots.forEach((dot, i) => {
      const distance = (time + .2) * dot.distance;
      context.strokeStyle = colors[i % colors.length];
      context.beginPath();
      context.moveTo(width / 2 + dot.x * distance, height / 2 + dot.y * distance);
      context.lineTo(width / 2 + dot.x * (distance + 12), height / 2 + dot.y * (distance + 12));
      context.stroke();
    });
    if (time < 1) starFrame = requestAnimationFrame(frame);
  }
  starFrame = requestAnimationFrame(frame);
}

let selected = tabs.find(tab => tab.getAttribute('aria-selected') === 'true');
tabs.forEach(tab => {
  tab.addEventListener('click', () => {
    if (tab === selected) return;
    selected = tab;
    animateStars();
    if (motion.matches) return;
    const panel = document.getElementById(tab.dataset.target);
    panel?.querySelectorAll('.model-scene, .mission-scene').forEach(assembleModel);
  });
});

try {
  const saved = localStorage.getItem('cubesat-galaxy');
  if (['nebula', 'red', 'green'].includes(saved)) theme.value = saved;
} catch { /* Optional preference. */ }
document.documentElement.dataset.theme = theme.value;
theme.addEventListener('change', () => {
  document.documentElement.dataset.theme = theme.value;
  try { localStorage.setItem('cubesat-galaxy', theme.value); } catch { /* Optional preference. */ }
});
motion.addEventListener('change', () => {
  if (motion.matches) {
    cancelAnimationFrame(starFrame);
    canvasContext(stars);
  }
});
