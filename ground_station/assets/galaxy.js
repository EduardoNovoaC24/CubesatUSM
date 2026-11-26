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

function interpolate(a, b, t) {
  return { x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t };
}

function sampleSegment(a, b) {
  const count = Math.max(2, Math.ceil(Math.hypot(b.x - a.x, b.y - a.y) / 4));
  return Array.from({ length: count }, (_, i) => interpolate(a, b, i / (count - 1)));
}

function projectedCorners(element, origin) {
  // Measure four points after the browser applies the scene's actual 3D transforms.
  const markers = [[0, 0], [1, 0], [1, 1], [0, 1]].map(([x, y]) => {
    const marker = document.createElement('span');
    marker.style.cssText = `position:absolute;width:0;height:0;left:${x * 100}%;top:${y * 100}%;pointer-events:none`;
    element.append(marker);
    return marker;
  });
  const corners = markers.map(marker => {
    const rect = marker.getBoundingClientRect();
    return { x: rect.left - origin.left, y: rect.top - origin.top };
  });
  markers.forEach(marker => marker.remove());
  return corners;
}

function outline(corners) {
  return corners.flatMap((point, i) => sampleSegment(point, corners[(i + 1) % corners.length]));
}

function modelPoints(scene, origin) {
  return [...scene.querySelectorAll('.sat-face')].flatMap(face => {
    const corners = projectedCorners(face, origin);
    const [a, b, c, d] = corners;
    if ((b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x) <= 0) return [];
    const grid = outline(corners);
    const rows = face.offsetHeight > face.offsetWidth * 2 ? 7 : 3;
    for (let i = 1; i < rows; i++) {
      grid.push(...sampleSegment(interpolate(a, d, i / rows), interpolate(b, c, i / rows)));
    }
    for (let i = 1; i < 4; i++) {
      grid.push(...sampleSegment(interpolate(a, b, i / 4), interpolate(d, c, i / 4)));
    }
    return grid;
  });
}

function missionPoints(scene, origin) {
  const linework = [...scene.querySelectorAll('.mission-tripod i, .mission-tripod b, .mission-tether, .mission-module')].flatMap(element => {
    if (element.matches('i')) {
      const [a, b, c, d] = projectedCorners(element, origin);
      return sampleSegment(interpolate(a, b, .5), interpolate(d, c, .5));
    }
    const { left, top, right, bottom } = element.getBoundingClientRect();
    return outline([[left, top], [right, top], [right, bottom], [left, bottom]]
      .map(([x, y]) => ({ x: x - origin.left, y: y - origin.top })));
  });
  for (const element of scene.querySelectorAll('.mission-photo, .wheel-rotor')) {
    const rect = element.getBoundingClientRect();
    const radius = element.offsetWidth / 2;
    const x = rect.left + rect.width / 2 - origin.left;
    const y = rect.top + rect.height / 2 - origin.top;
    for (let i = 0; i < 32; i++) {
      const angle = i * 2 * Math.PI / 32;
      linework.push({ x: x + Math.cos(angle) * radius, y: y + Math.sin(angle) * radius });
    }
  }
  return linework;
}

function assembleModel(scene) {
  if (scene.querySelector('.model-particles')) return;
  const canvas = document.createElement('canvas');
  canvas.className = 'model-particles';
  canvas.setAttribute('aria-hidden', 'true');
  scene.append(canvas);
  const { context, width, height } = canvasContext(canvas);
  if (!context || !width || !height) {
    canvas.remove();
    return;
  }
  const origin = canvas.getBoundingClientRect();
  const points = scene.matches('.mission-scene') ? missionPoints(scene, origin) : modelPoints(scene, origin);
  if (!points.length) {
    canvas.remove();
    return;
  }
  const particles = points.map(point => ({
    ...point, sx: Math.random() * width, sy: Math.random() * height, size: .65 + Math.random() * 1.1,
  }));
  scene.classList.add('assembling');
  const colors = palette();
  const start = performance.now();
  function frame(now) {
    const time = motion.matches ? 1 : Math.min(1, (now - start) / 1300);
    const progress = 1 - (1 - time) ** 3;
    if (time >= .76) scene.classList.remove('assembling');
    if (time === 1) {
      canvas.remove();
      return;
    }
    context.clearRect(0, 0, width, height);
    particles.forEach((particle, i) => {
      const x = particle.sx + (particle.x - particle.sx) * progress;
      const y = particle.sy + (particle.y - particle.sy) * progress;
      context.fillStyle = colors[i % colors.length];
      context.globalAlpha = (.3 + progress * .62) * Math.min(1, (1 - time) / .24);
      context.beginPath();
      context.arc(x, y, particle.size, 0, Math.PI * 2);
      context.fill();
    });
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
