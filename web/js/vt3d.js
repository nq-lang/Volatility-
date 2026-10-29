// vt3d.js — tiny dependency-free 3D layer shared by the surface panels and the
// PCA biplot. One projection + one interaction model (drag/touch rotate,
// wheel/pinch zoom, tap, double-tap auto-frame) so every 3D view behaves the
// same on desktop and phone, and so what is screenshot-tested offline is
// literally the code that ships (no separate offline stand-in).
//
// Convention: camera sits on +z looking toward the origin. After rotation a
// LARGER z2 means CLOSER to the viewer, so painter's algorithm draws ascending z2.

class Cam3D {
    constructor({ yaw = 0.6, pitch = 0.5, zoom = 1, radius = 60 } = {}) {
        this.home = { yaw, pitch, zoom };
        this.yaw = yaw; this.pitch = pitch; this.zoom = zoom;
        this.radius = radius;           // bounding radius of the scene, used for auto-framing
        this.center = [0, 0, 0];        // world point kept at screen center
        this.roll = 0;                  // in-plane spin of the whole view (radians); 0 for every pre-existing panel
        this.persp = 1;                 // 1 = full perspective (default), 0 = orthographic; blend in between
    }
    reset() { this.yaw = this.home.yaw; this.pitch = this.home.pitch; this.zoom = this.home.zoom; }
    // Rotate a world point into camera space (before perspective).
    rotate(x, y, z) {
        x -= this.center[0]; y -= this.center[1]; z -= this.center[2];
        const cy = Math.cos(this.yaw), sy = Math.sin(this.yaw);
        const x1 = x * cy + z * sy, z1 = -x * sy + z * cy;
        const cp = Math.cos(this.pitch), sp = Math.sin(this.pitch);
        const y2 = y * cp - z1 * sp, z2 = y * sp + z1 * cp;
        return [x1, y2, z2];
    }
    // Project to screen. Returns {sx, sy, z, s} where s is the local pixel scale.
    project(x, y, z, w, h) {
        let [x1, y2, z2] = this.rotate(x, y, z);
        if (this.roll) { const cr = Math.cos(this.roll), sr = Math.sin(this.roll); const xr = x1 * cr - y2 * sr; y2 = x1 * sr + y2 * cr; x1 = xr; }
        const D = this.radius * 3.2;
        const base = (Math.min(w, h) / (2 * this.radius)) * 0.92 * this.zoom;
        const pf = D / Math.max(D - z2, D * 0.15);
        const s = base * (1 + (pf - 1) * this.persp);
        return { sx: w / 2 + x1 * s, sy: h / 2 - y2 * s, z: z2, s };
    }
}

// Attach mouse + touch interaction. Callbacks are all optional:
//   onChange()        camera moved -> redraw
//   onTap(x, y)       short tap/click with negligible movement (canvas CSS px)
//   onDoubleTap()     double tap / double click -> auto-frame
//   onHover(x, y)     pointer moved without buttons (desktop)
function attachOrbit(canvas, cam, cb = {}) {
    const pts = new Map();
    let downAt = null, moved = 0, lastTap = 0, pinchStart = null;
    const local = (e) => { const r = canvas.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };
    canvas.style.touchAction = 'none';

    canvas.addEventListener('pointerdown', (e) => {
        canvas.setPointerCapture?.(e.pointerId);
        pts.set(e.pointerId, local(e));
        if (pts.size === 1) { downAt = { t: performance.now(), p: local(e) }; moved = 0; }
        if (pts.size === 2) {
            const [a, b] = [...pts.values()];
            pinchStart = { d: Math.hypot(a[0] - b[0], a[1] - b[1]), zoom: cam.zoom };
        }
    });
    canvas.addEventListener('pointermove', (e) => {
        if (!pts.has(e.pointerId)) { cb.onHover && cb.onHover(...local(e)); return; }
        const prev = pts.get(e.pointerId), cur = local(e);
        pts.set(e.pointerId, cur);
        if (pts.size === 1) {
            const dx = cur[0] - prev[0], dy = cur[1] - prev[1];
            moved += Math.abs(dx) + Math.abs(dy);
            cam.yaw += dx * 0.008;
            cam.pitch = Math.max(-1.45, Math.min(1.45, cam.pitch + dy * 0.008));
            cb.onChange && cb.onChange();
        } else if (pts.size === 2 && pinchStart) {
            const [a, b] = [...pts.values()];
            const d = Math.hypot(a[0] - b[0], a[1] - b[1]);
            cam.zoom = Math.max(0.35, Math.min(6, pinchStart.zoom * d / (pinchStart.d || 1)));
            moved += 10;
            cb.onChange && cb.onChange();
        }
    });
    const end = (e) => {
        const wasSingle = pts.size === 1;
        pts.delete(e.pointerId);
        if (pts.size < 2) pinchStart = null;
        if (wasSingle && downAt && moved < 6 && performance.now() - downAt.t < 400) {
            const now = performance.now();
            if (now - lastTap < 350) { lastTap = 0; cb.onDoubleTap && cb.onDoubleTap(); }
            else { lastTap = now; cb.onTap && cb.onTap(downAt.p[0], downAt.p[1]); }
        }
    };
    canvas.addEventListener('pointerup', end);
    canvas.addEventListener('pointercancel', end);
    canvas.addEventListener('wheel', (e) => {
        e.preventDefault();
        cam.zoom = Math.max(0.35, Math.min(6, cam.zoom * (e.deltaY < 0 ? 1.1 : 1 / 1.1)));
        cb.onChange && cb.onChange();
    }, { passive: false });
}

// Small helpers shared by renderers.
function vtSizeCanvas(canvas, container) {
    const r = container.getBoundingClientRect();
    const w = Math.max(200, r.width), h = Math.max(160, r.height);
    const dpr = 2;
    if (canvas.width !== w * dpr || canvas.height !== h * dpr) { canvas.width = w * dpr; canvas.height = h * dpr; }
    canvas.style.width = w + 'px'; canvas.style.height = h + 'px';
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { ctx, w, h };
}
function vtMix(a, b, t) { return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t]; }
function vtRgb(c, a = 1) { return `rgba(${Math.round(c[0])},${Math.round(c[1])},${Math.round(c[2])},${a})`; }
