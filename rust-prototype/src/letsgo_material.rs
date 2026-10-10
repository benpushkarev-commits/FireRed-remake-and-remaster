//! Let's Go field shading (Settings H / `letsgo_shading`, default on).
//! tools/letsgo/gf2glb.py writes each Let's Go material's real texture set and parameters
//! into glTF material extras (`lg`); this swaps those glTF StandardMaterials for
//! `FieldMaterial` (assets/shaders/letsgo_field.wgsl). Also gives every texture mipmaps and
//! anisotropic filtering, which removes the distant shimmer.
use super::*;
use bevy::gltf::{GltfAssetLabel, GltfMaterialExtras};
use bevy::image::{ImageAddressMode, ImageFilterMode, ImageSampler, ImageSamplerDescriptor};
use bevy::pbr::{ExtendedMaterial, MaterialExtension};
use bevy::render::render_resource::{AsBindGroup, ShaderType, TextureFormat};
use bevy::shader::ShaderRef;
use std::collections::{HashMap, HashSet};

pub(super) type FieldMaterial = ExtendedMaterial<StandardMaterial, LgExt>;

const F_ON: u32 = 256;
const F_BLEND5: u32 = 512;
const F_SIDE_UV1: u32 = 1024;
const F_CLOUD: u32 = 2048;
const KIND_TREE: u32 = 3;
const KIND_BLEND2: u32 = 4;
const KIND_WATER: u32 = 5;
const KIND_WAVE: u32 = 6;
const KIND_SKY: u32 = 7;
const KIND_CHAR: u32 = 8;
const KIND_FIRE: u32 = 9;
/// Brightness of a fully sun-lit white surface after exposure (see shader `L`).
const LIGHT_REF: f32 = 1.2;
/// x: shadow colour gain, y: rim gain, z: overall gain, w: vertex colour on plain objects.
const TUNE: Vec4 = Vec4::new(1.7, 0.6, 0.93, 1.0);

#[derive(Clone, Copy, ShaderType, Debug, Default, Reflect)]
pub(super) struct LgParams {
    kind: u32,
    flags: u32,
    discard: f32,
    light_ref: f32,
    shadow: Vec4,
    rim_color: Vec4,
    rim: Vec4,
    base: Vec4,
    deep: Vec4,
    scroll_a: Vec4,
    scroll_b: Vec4,
    tune: Vec4,
}

#[derive(Asset, AsBindGroup, Reflect, Debug, Clone, Default)]
pub(super) struct LgExt {
    #[uniform(100)]
    p: LgParams,
    #[texture(101)]
    #[sampler(102)]
    t0: Option<Handle<Image>>,
    #[texture(103)]
    #[sampler(104)]
    t1: Option<Handle<Image>>,
    #[texture(105)]
    #[sampler(106)]
    t2: Option<Handle<Image>>,
    #[texture(107)]
    #[sampler(108)]
    t3: Option<Handle<Image>>,
    #[texture(109)]
    #[sampler(110)]
    t4: Option<Handle<Image>>,
    #[texture(111)]
    #[sampler(112)]
    t5: Option<Handle<Image>>,
    #[texture(113)]
    #[sampler(114)]
    cloud: Option<Handle<Image>>,
}

impl MaterialExtension for LgExt {
    fn fragment_shader() -> ShaderRef {
        "shaders/letsgo_field.wgsl".into()
    }
}

/// Mesh entity already handled (swapped or not a Let's Go material).
#[derive(Component)]
pub(super) struct LgSeen;

/// Images that must clamp (light ramps / toon tables) and the ones already given mips.
#[derive(Resource, Default)]
pub(super) struct LgImages {
    clamp: HashSet<AssetId<Image>>,
    clamped: HashSet<AssetId<Image>>,
    done: HashSet<AssetId<Image>>,
}

fn v4(v: &serde_json::Value, w: f32) -> Vec4 {
    let a: Vec<f32> = v.as_array().into_iter().flatten().filter_map(|x| x.as_f64()).map(|x| x as f32).collect();
    Vec4::new(*a.first().unwrap_or(&0.), *a.get(1).unwrap_or(&0.), *a.get(2).unwrap_or(&0.), *a.get(3).unwrap_or(&w))
}

pub(super) fn swap(
    mut c: Commands,
    meshes: Query<(Entity, &MeshMaterial3d<StandardMaterial>, &GltfMaterialExtras), Without<LgSeen>>,
    parents: Query<&ChildOf>,
    roots: Query<&SceneRoot>,
    standard: Res<Assets<StandardMaterial>>,
    mut field: ResMut<Assets<FieldMaterial>>,
    server: Res<AssetServer>,
    p: Res<Preferences>,
    mut lg_images: ResMut<LgImages>,
    mut cache: Local<HashMap<AssetId<StandardMaterial>, (Handle<FieldMaterial>, u32)>>,
) {
    for (entity, material, extras) in &meshes {
        if let Some((handle, kind)) = cache.get(&material.0.id()) {
            c.entity(entity).remove::<MeshMaterial3d<StandardMaterial>>().insert((MeshMaterial3d(handle.clone()), LgSeen));
            if *kind == KIND_SKY {
                c.entity(entity).insert((bevy::light::NotShadowCaster, bevy::light::NotShadowReceiver));
            }
            continue;
        }
        let Ok(json) = serde_json::from_str::<serde_json::Value>(&extras.value) else {
            c.entity(entity).insert(LgSeen);
            continue;
        };
        let lg = &json["lg"];
        if !lg.is_object() {
            c.entity(entity).insert(LgSeen);
            continue;
        }
        // The glTF file this mesh came from: walk up to the SceneRoot.
        let mut at = entity;
        let mut path = None;
        for _ in 0..64 {
            if let Ok(root) = roots.get(at) {
                path = root.0.path().map(|p| p.without_label().into_owned());
                break;
            }
            match parents.get(at) {
                Ok(parent) => at = parent.parent(),
                Err(_) => break,
            }
        }
        let (Some(path), Some(base)) = (path, standard.get(&material.0)) else { continue };
        let kind = lg["kind"].as_u64().unwrap_or(0) as u32;
        let slots: Vec<i64> = lg["tex"].as_array().into_iter().flatten().map(|x| x.as_i64().unwrap_or(-1)).collect();
        let load = |i: i64| -> Option<Handle<Image>> {
            (i >= 0).then(|| server.load(GltfAssetLabel::Texture(i as usize).from_asset(path.clone())))
        };
        let tex: Vec<Option<Handle<Image>>> = (0..6).map(|i| load(*slots.get(i).unwrap_or(&-1))).collect();
        let cloud = load(lg["cloud"].as_i64().unwrap_or(-1));
        if kind == KIND_TREE || kind == KIND_CHAR {
            for t in [&tex[1], &tex[3], &tex[4]].into_iter().flatten().filter(|_| kind == KIND_TREE).chain(tex[3].iter().filter(|_| kind == KIND_CHAR)) {
                lg_images.clamp.insert(t.id());
            }
        }
        let mut flags = tex.iter().enumerate().filter(|(_, t)| t.is_some()).fold(0u32, |f, (i, _)| f | 1 << i);
        if p.letsgo_shading { flags |= F_ON; }
        let blend5 = lg["blend5"].as_bool().unwrap_or(false) && matches!(kind, KIND_BLEND2 | KIND_WATER | KIND_WAVE | KIND_FIRE);
        if blend5 { flags |= F_BLEND5; }
        if lg["side_uv"].as_u64() == Some(1) { flags |= F_SIDE_UV1; }
        if cloud.is_some() { flags |= F_CLOUD; }
        let discard = lg["discard"].as_f64().unwrap_or(0.) as f32;
        let discard = if matches!(base.alpha_mode, AlphaMode::Mask(_)) { discard.clamp(0.0, 0.6).max(if discard > 0. { 0.35 } else { 0. }) } else { 0. };
        let scroll: Vec<f32> = lg["scroll"].as_array().into_iter().flatten().filter_map(|x| x.as_f64()).map(|x| x as f32).collect();
        let sc = |i: usize| *scroll.get(i).unwrap_or(&0.);
        let mut std = base.clone();
        if blend5 {
            std.alpha_mode = AlphaMode::Blend;
        }
        let handle = field.add(FieldMaterial {
            base: std,
            extension: LgExt {
                p: LgParams {
                    kind,
                    flags,
                    discard,
                    light_ref: LIGHT_REF,
                    shadow: v4(&lg["shadow"], 1.),
                    rim_color: v4(&lg["rim_color"], 1.),
                    rim: v4(&lg["rim"], 0.),
                    base: v4(&lg["base_color"], 1.),
                    deep: v4(&lg["deep_color"], 1.),
                    scroll_a: Vec4::new(sc(0), sc(1), sc(2), sc(3)),
                    scroll_b: Vec4::new(sc(4), sc(5), sc(6), sc(7)),
                    tune: TUNE,
                },
                t0: tex[0].clone(),
                t1: tex[1].clone(),
                t2: tex[2].clone(),
                t3: tex[3].clone(),
                t4: tex[4].clone(),
                t5: tex[5].clone(),
                cloud,
            },
        });
        cache.insert(material.0.id(), (handle.clone(), kind));
        c.entity(entity).remove::<MeshMaterial3d<StandardMaterial>>().insert((MeshMaterial3d(handle), LgSeen));
        if kind == KIND_SKY {
            c.entity(entity).insert((bevy::light::NotShadowCaster, bevy::light::NotShadowReceiver));
        }
    }
}

/// Settings toggle: flip the shading flag on every field material.
pub(super) fn toggle(p: Res<Preferences>, mut field: ResMut<Assets<FieldMaterial>>, mut last: Local<Option<bool>>) {
    if *last == Some(p.letsgo_shading) {
        return;
    }
    let first = last.is_none();
    *last = Some(p.letsgo_shading);
    if first {
        return;
    }
    for (_, m) in field.iter_mut() {
        if p.letsgo_shading { m.extension.p.flags |= F_ON; } else { m.extension.p.flags &= !F_ON; }
    }
}

/// Mipmaps + 8x anisotropic filtering for every loaded RGBA8 texture; clamp for light ramps.
/// Alpha-tested foliage keeps its coverage in the smaller mips.
pub(super) fn images(mut events: MessageReader<AssetEvent<Image>>, mut images: ResMut<Assets<Image>>, mut lg_images: ResMut<LgImages>, server: Res<AssetServer>) {
    // Ramps registered after their image loaded.
    let pending: Vec<AssetId<Image>> = lg_images.clamp.difference(&lg_images.clamped).copied().collect();
    for id in pending {
        let Some(image) = images.get_mut(id) else { continue };
        let mut d = match &image.sampler { ImageSampler::Descriptor(d) => d.clone(), ImageSampler::Default => ImageSamplerDescriptor::linear() };
        d.address_mode_u = ImageAddressMode::ClampToEdge;
        d.address_mode_v = ImageAddressMode::ClampToEdge;
        image.sampler = ImageSampler::Descriptor(d);
        lg_images.clamped.insert(id);
    }
    let ids: Vec<AssetId<Image>> = events.read().filter_map(|e| match e {
        AssetEvent::Added { id } | AssetEvent::LoadedWithDependencies { id } => Some(*id),
        _ => None,
    }).collect();
    for id in ids {
        // Only file / glTF textures (never font atlases or render targets).
        if lg_images.done.contains(&id) || server.get_path(id).is_none() {
            continue;
        }
        let Some(image) = images.get(id) else { continue };
        let fmt = image.texture_descriptor.format;
        let ok_fmt = matches!(fmt, TextureFormat::Rgba8UnormSrgb | TextureFormat::Rgba8Unorm);
        let size = image.texture_descriptor.size;
        let usage_ok = !image.texture_descriptor.usage.contains(bevy::render::render_resource::TextureUsages::RENDER_ATTACHMENT);
        if !ok_fmt || !usage_ok || image.texture_descriptor.mip_level_count != 1 || size.depth_or_array_layers != 1
            || image.data.is_none() || size.width < 4 || size.height < 4 {
            continue;
        }
        let clamp = lg_images.clamp.contains(&id);
        let Some(mut image) = images.get_mut(id) else { continue };
        let w = size.width as usize;
        let h = size.height as usize;
        let data = image.data.take().unwrap_or_default();
        let (all, levels) = build_mips(&data, w, h);
        image.data = Some(all);
        image.texture_descriptor.mip_level_count = levels;
        let mut d = match &image.sampler {
            ImageSampler::Descriptor(d) => d.clone(),
            ImageSampler::Default => ImageSamplerDescriptor::linear(),
        };
        d.mag_filter = ImageFilterMode::Linear;
        d.min_filter = ImageFilterMode::Linear;
        d.mipmap_filter = ImageFilterMode::Linear;
        d.anisotropy_clamp = 8;
        if clamp {
            d.address_mode_u = ImageAddressMode::ClampToEdge;
            d.address_mode_v = ImageAddressMode::ClampToEdge;
            lg_images.clamped.insert(id);
        }
        image.sampler = ImageSampler::Descriptor(d);
        lg_images.done.insert(id);
    }
}

/// Box-filtered mip chain; alpha is rescaled per level to keep the >=0.5 coverage of level 0.
fn build_mips(data: &[u8], w: usize, h: usize) -> (Vec<u8>, u32) {
    let coverage = |px: &[u8], scale: f32| px.chunks_exact(4).filter(|p| p[3] as f32 * scale >= 127.5).count() as f32 / (px.len() / 4).max(1) as f32;
    let cut = data.chunks_exact(4).any(|p| p[3] > 8 && p[3] < 247) || data.chunks_exact(4).any(|p| p[3] < 128);
    let target = if cut { coverage(data, 1.0) } else { 1.0 };
    let mut out = data.to_vec();
    let mut level = data.to_vec();
    let (mut lw, mut lh) = (w, h);
    let mut levels = 1;
    while lw > 1 || lh > 1 {
        let (nw, nh) = ((lw / 2).max(1), (lh / 2).max(1));
        let mut next = vec![0u8; nw * nh * 4];
        for y in 0..nh {
            for x in 0..nw {
                for ch in 0..4 {
                    let mut s = 0u32;
                    for (dx, dy) in [(0, 0), (1, 0), (0, 1), (1, 1)] {
                        let sx = (x * 2 + dx).min(lw - 1);
                        let sy = (y * 2 + dy).min(lh - 1);
                        s += level[(sy * lw + sx) * 4 + ch] as u32;
                    }
                    next[(y * nw + x) * 4 + ch] = (s / 4) as u8;
                }
            }
        }
        if cut && target > 0.0 && target < 1.0 {
            let (mut lo, mut hi) = (1.0f32, 4.0f32);
            for _ in 0..12 {
                let mid = (lo + hi) / 2.;
                if coverage(&next, mid) < target { lo = mid } else { hi = mid }
            }
            for p in next.chunks_exact_mut(4) {
                p[3] = (p[3] as f32 * hi).min(255.) as u8;
            }
        }
        out.extend_from_slice(&next);
        level = next;
        lw = nw;
        lh = nh;
        levels += 1;
    }
    (out, levels)
}

/// Let's Go cloud sky dome around the field camera (outdoors, Let's Go scenery on).
#[derive(Component)]
pub(super) struct FieldSky;

pub(super) fn sky(
    mut c: Commands,
    p: Res<Preferences>,
    s: Res<Simulation>,
    battle: Res<battle_screen::Screen>,
    server: Res<AssetServer>,
    camera: Query<&GlobalTransform, With<SceneCamera>>,
    mut domes: Query<(&mut Transform, &mut Visibility), With<FieldSky>>,
    mut spawned: Local<bool>,
) {
    if !*spawned {
        *spawned = true;
        let base = std::env::current_exe().ok().and_then(|p| p.parent().map(|p| p.join("assets")));
        let file = base.filter(|b| b.is_dir()).unwrap_or_else(|| "assets".into()).join("letsgo/battle/btl_sky.glb");
        if file.exists() {
            c.spawn((FieldSky, SceneRoot(server.load(GltfAssetLabel::Scene(0).from_asset("letsgo/battle/btl_sky.glb"))),
                Transform::default(), Visibility::Hidden));
        }
        return;
    }
    let indoor = s.world.map(&s.game.map).indoor;
    let show = p.letsgo_scenery && p.letsgo_shading && !indoor && !battle.active;
    let at = camera.iter().next().map(|t| t.translation());
    for (mut t, mut v) in &mut domes {
        let want = if show { Visibility::Inherited } else { Visibility::Hidden };
        if *v != want { *v = want; }
        if let Some(at) = at {
            t.translation = Vec3::new(at.x, at.y - 3., at.z);
        }
    }
}
