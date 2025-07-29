// SPDX-License-Identifier: GPL-2.0-or-later

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_sro.h>
#include <drm/drm_atomic_sro_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>

#include <linux/gfp.h>
#include <linux/sched/mm.h>

/**
 * drm_atomic_helper_sro_readout_planes_state - read out all plane states from hardware
 * @dev: DRM device
 * @state: SRO state container to fill
 *
 * Iterates over all planes, allocates a state for each via
 * atomic_create_state, and calls the plane's
 * atomic_sro_readout_state hook to fill it from hardware.
 *
 * RETURNS:
 *
 * 0 on success, a negative error code otherwise.
 */
int
drm_atomic_helper_sro_readout_planes_state(struct drm_device *dev,
					   struct drm_atomic_sro_state *state)
{
	struct drm_plane *plane;

	might_alloc(GFP_KERNEL);

	drm_for_each_plane(plane, dev) {
		const struct drm_plane_funcs *plane_funcs = plane->funcs;
		struct drm_plane_state *plane_state;
		int ret;

		if (!plane_funcs->atomic_sro_readout_state) {
			drm_warn(dev, "No plane readout implementation.");
			continue;
		}

		drm_dbg_kms(dev, "Initializing Plane %s state.\n", plane->name);

		plane_state = plane_funcs->atomic_create_state(plane);
		if (drm_WARN_ON(dev, IS_ERR(plane_state)))
			return 0;

		ret = plane_funcs->atomic_sro_readout_state(plane, state,
							    plane_state);
		if (drm_WARN_ON(dev, ret)) {
			plane_funcs->atomic_destroy_state(plane, plane_state);
			return ret;
		}

		drm_atomic_sro_set_plane_state(state, plane, plane_state);
	}

	return 0;
}

/**
 * drm_atomic_helper_sro_readout_crtcs_state - read out all CRTC states from hardware
 * @dev: DRM device
 * @state: SRO state container to fill
 *
 * Iterates over all CRTCs, allocates a state for each via
 * atomic_create_state, and calls the CRTC's
 * atomic_sro_readout_state hook to fill it from hardware.
 *
 * RETURNS:
 *
 * 0 on success, a negative error code otherwise.
 */
int
drm_atomic_helper_sro_readout_crtcs_state(struct drm_device *dev,
					  struct drm_atomic_sro_state *state)
{
	struct drm_crtc *crtc;

	might_alloc(GFP_KERNEL);

	drm_for_each_crtc(crtc, dev) {
		const struct drm_crtc_funcs *crtc_funcs = crtc->funcs;
		struct drm_crtc_state *crtc_state;
		int ret;

		if (!crtc_funcs->atomic_sro_readout_state) {
			drm_warn(dev, "No CRTC readout implementation.");
			continue;
		}

		drm_dbg_kms(dev, "Initializing CRTC %s state.\n", crtc->name);

		crtc_state = crtc_funcs->atomic_create_state(crtc);
		if (drm_WARN_ON(dev, IS_ERR(crtc_state)))
			return PTR_ERR(crtc_state);

		ret = crtc_funcs->atomic_sro_readout_state(crtc, state,
							   crtc_state);
		if (drm_WARN_ON(dev, ret)) {
			crtc_funcs->atomic_destroy_state(crtc, crtc_state);
			return ret;
		}

		drm_atomic_sro_set_crtc_state(state, crtc, crtc_state);
	}

	return 0;
}

/**
 * drm_atomic_helper_sro_readout_connectors_state - read out all connector states from hardware
 * @dev: DRM device
 * @state: SRO state container to fill
 *
 * Iterates over all connectors, allocates a state for each via
 * atomic_create_state, and calls the connector's
 * atomic_sro_readout_state hook to fill it from hardware.
 *
 * RETURNS:
 *
 * 0 on success, a negative error code otherwise.
 */
int
drm_atomic_helper_sro_readout_connectors_state(struct drm_device *dev,
					       struct drm_atomic_sro_state *state)
{
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;

	might_alloc(GFP_KERNEL);

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		const struct drm_connector_funcs *conn_funcs = connector->funcs;
		struct drm_connector_state *conn_state;
		int ret;

		if (!conn_funcs->atomic_sro_readout_state) {
			drm_warn(dev, "No Connector readout implementation.");
			continue;
		}

		drm_dbg_kms(dev, "Initializing Connector %s state.\n", connector->name);

		conn_state = conn_funcs->atomic_create_state(connector);
		if (drm_WARN_ON(dev, IS_ERR(conn_state)))
			return PTR_ERR(conn_state);

		ret = conn_funcs->atomic_sro_readout_state(connector, state,
							   conn_state);
		if (drm_WARN_ON(dev, ret)) {
			conn_funcs->atomic_destroy_state(connector, conn_state);
			return ret;
		}

		drm_atomic_sro_set_connector_state(state, connector, conn_state);
	}
	drm_connector_list_iter_end(&conn_iter);


	return 0;
}

/**
 * drm_atomic_helper_sro_readout_bridges_state - read out all bridge states from hardware
 * @dev: DRM device
 * @state: SRO state container to fill
 *
 * Iterates over all encoders and their bridge chains, allocates a state
 * for each bridge via atomic_create_state, and calls the bridge's
 * atomic_sro_readout_state hook to fill it from hardware.
 *
 * Bridges are handled separately from other private objects because
 * bridge registration does not guarantee ordering. Traversing the
 * encoder bridge chains ensures each bridge can query the state of
 * preceding bridges in its chain.
 *
 * RETURNS:
 *
 * 0 on success, a negative error code otherwise.
 */
int
drm_atomic_helper_sro_readout_bridges_state(struct drm_device *dev,
					    struct drm_atomic_sro_state *state)
{
	struct drm_encoder *encoder;

	might_alloc(GFP_KERNEL);

	/*
	 * It works a bit differently for bridges. Indeed they rely on a
	 * drm_private_obj and drm_private_state, but bridge
	 * registration doesn't guarantee ordering.
	 *
	 * In order for each bridge callback to be able to query the
	 * previous bridge state, we thus need to read out the bridge
	 * state by looking at each encoder and then traversing its
	 * bridge list.
	 *
	 * And we'll then readout all the non-bridge drm_private_obj
	 * later.
	 */
	drm_for_each_encoder(encoder, dev) {
		struct drm_bridge *bridge;
		int ret;

		list_for_each_entry(bridge, &encoder->bridge_chain, chain_node) {
			struct drm_private_obj *bridge_obj = &bridge->base;
			const struct drm_private_state_funcs *bridge_obj_funcs =
				bridge_obj->funcs;
			struct drm_private_state *bridge_obj_state;

			if (!bridge_obj_funcs->atomic_sro_readout_state) {
				drm_warn(dev,
					 "No bridge %s readout implementation.",
					 bridge->name);
				continue;
			}

			drm_dbg_kms(dev, "Initializing Bridge %s", bridge->name);

			bridge_obj_state =
				bridge_obj_funcs->atomic_create_state(
					bridge_obj);
			if (drm_WARN_ON(dev, IS_ERR(bridge_obj_state)))
				return ret;

			ret = bridge_obj_funcs->atomic_sro_readout_state(
				bridge_obj, state, bridge_obj_state);
			if (drm_WARN_ON(dev, ret)) {
				bridge_obj_funcs->atomic_destroy_state(
					bridge_obj, bridge_obj_state);
				return ret;
			}

			drm_atomic_sro_set_private_obj_state(state, bridge_obj,
							     bridge_obj_state);
		}
	}

	return 0;
}

/**
 * drm_atomic_helper_sro_readout_private_objs_state - read out non-bridge private object states
 * @dev: DRM device
 * @state: SRO state container to fill
 *
 * Iterates over all private objects that are not bridges (bridges are
 * handled by drm_atomic_helper_sro_readout_bridges_state()), allocates
 * a state for each via atomic_create_state, and calls the object's
 * atomic_sro_readout_state hook to fill it from hardware.
 *
 * RETURNS:
 *
 * 0 on success, a negative error code otherwise.
 */
int
drm_atomic_helper_sro_readout_private_objs_state(struct drm_device *dev,
						 struct drm_atomic_sro_state *state)
{
	struct drm_private_obj *privobj;

	might_alloc(GFP_KERNEL);

	drm_for_each_privobj(privobj, dev) {
		const struct drm_private_state_funcs *priv_funcs =
			privobj->funcs;
		struct drm_private_state *priv_state;
		int ret;

		/*
		 * We already accounted readout the bridge state earlier, we only
		 * have to deal with !bridges drm_private_obj now.
		 */
		if (drm_private_obj_is_bridge(privobj))
			continue;

		if (!priv_funcs->atomic_sro_readout_state) {
			drm_warn(dev,
				 "No private object %s readout implementation.",
				 privobj->name);
			continue;
		}

		drm_dbg_kms(dev, "Initializing Private Object %s",
			    privobj->name);

		priv_state = priv_funcs->atomic_create_state(privobj);
		if (drm_WARN_ON(dev, IS_ERR(priv_state)))
			return PTR_ERR(priv_state);

		ret = priv_funcs->atomic_sro_readout_state(privobj, state,
							   priv_state);
		if (drm_WARN_ON(dev, ret)) {
			priv_funcs->atomic_destroy_state(privobj, priv_state);
			return ret;
		}

		drm_atomic_sro_set_private_obj_state(state, privobj,
						     priv_state);
	}

	return 0;
}

/**
 * drm_atomic_helper_sro_build_state - build an SRO state from hardware
 * @dev: DRM device to build the state for
 *
 * Allocates a &struct drm_atomic_sro_state and calls the readout hooks
 * for CRTCs, planes, connectors, bridges, and private objects in
 * sequence to fill it from the current hardware state.
 *
 * This is the default implementation for
 * &drm_mode_config_helper_funcs.atomic_sro_build_state.
 *
 * RETURNS:
 *
 * A &struct drm_atomic_sro_state on success, an error pointer otherwise.
 */
struct drm_atomic_sro_state *
drm_atomic_helper_sro_build_state(struct drm_device *dev)
{
	struct drm_atomic_sro_state *state;
	struct drm_printer p = drm_dbg_printer(dev, DRM_UT_ATOMIC, NULL);
	int ret;

	drm_dbg_kms(dev, "Starting to build atomic state from hardware state.\n");

	state = drm_atomic_sro_state_alloc(dev);
	if (drm_WARN_ON(dev, !state))
		return ERR_PTR(-ENOMEM);

	ret = drm_atomic_helper_sro_readout_crtcs_state(dev, state);
	if (ret)
		goto err_state_free;

	ret = drm_atomic_helper_sro_readout_planes_state(dev, state);
	if (ret)
		goto err_state_free;

	ret = drm_atomic_helper_sro_readout_connectors_state(dev, state);
	if (ret)
		goto err_state_free;

	ret = drm_atomic_helper_sro_readout_bridges_state(dev, state);
	if (ret)
		goto err_state_free;

	ret = drm_atomic_helper_sro_readout_private_objs_state(dev, state);
	if (ret)
		goto err_state_free;

	drm_atomic_sro_state_print(state, &p);

	return state;

err_state_free:
	drm_atomic_sro_state_free(state);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(drm_atomic_helper_sro_build_state);

/**
 * drm_atomic_helper_sro_readout_state - default &drm_mode_config_funcs.atomic_sro_readout_state implementation
 * @dev: DRM device to read out state for
 *
 * Checks if the device supports hardware state readout, builds the SRO
 * state via &drm_mode_config_helper_funcs.atomic_sro_build_state,
 * installs it as the current state of all DRM objects, and frees the
 * SRO container.
 *
 * If the device does not support readout, this is a no-op.
 *
 * RETURNS:
 *
 * 0 on success, a negative error code otherwise.
 */
int drm_atomic_helper_sro_readout_state(struct drm_device *dev)
{
	const struct drm_mode_config_helper_funcs *funcs =
		dev->mode_config.helper_private;
	struct drm_atomic_sro_state *state;

	if (!drm_atomic_sro_device_can_readout(dev)) {
		drm_info(dev, "Device doesn't support hardware state readout.");
		return 0;
	}

	state = funcs->atomic_sro_build_state(dev);
	if (IS_ERR(state))
		return PTR_ERR(state);

	drm_atomic_sro_install_state(state);
	drm_atomic_sro_state_free(state);

	return 0;
}
EXPORT_SYMBOL(drm_atomic_helper_sro_readout_state);

static bool drm_atomic_helper_fb_compare(struct drm_printer *p,
					 struct drm_framebuffer *expected,
					 struct drm_framebuffer *actual)
{
	unsigned int i;
	bool ret = true;

	STATE_CHECK_FORMAT_INFO(ret, p, "framebuffer", expected, actual, format);

	for (i = 0; i < expected->format->num_planes; i++) {
		STATE_CHECK_U32(ret, p, "framebuffer", expected, actual, pitches[i]);
		STATE_CHECK_U32(ret, p, "framebuffer", expected, actual, offsets[i]);
	}

	STATE_CHECK_U64(ret, p, "framebuffer", expected, actual, modifier);
	STATE_CHECK_U32(ret, p, "framebuffer", expected, actual, width);
	STATE_CHECK_U32(ret, p, "framebuffer", expected, actual, height);
	STATE_CHECK_S32_X(ret, p, "framebuffer", expected, actual, flags);

	return ret;
}

/**
 * drm_atomic_helper_plane_compare_state - default &drm_plane_funcs.atomic_sro_compare_state hook
 * @plane: the plane being compared
 * @p: printer for reporting mismatches
 * @expected: the committed &struct drm_plane_state
 * @actual: the &struct drm_plane_state read out from hardware
 *
 * Compares the base &struct drm_plane_state fields of @actual to
 * @expected and reports any mismatches through @p. Drivers subclassing
 * the plane state should call this first, then compare their own fields.
 *
 * RETURNS:
 *
 * True if the states are identical, false otherwise.
 */
bool drm_atomic_helper_plane_compare_state(struct drm_plane *plane,
					   struct drm_printer *p,
					   struct drm_plane_state *expected,
					   struct drm_plane_state *actual)
{
	bool ret = true;

	STATE_CHECK_PTR(ret, p, plane->name, expected, actual, plane);
	STATE_CHECK_PTR(ret, p, plane->name, expected, actual, crtc);

	if (expected->fb && actual->fb) {
		if (!drm_atomic_helper_fb_compare(p, expected->fb, actual->fb))
			ret = false;
	} else if (!(!expected->fb && !actual->fb)) {
		drm_atomic_helper_print_state_mismatch(p,
						       plane->name,
						       "fb",
						       "expected framebuffer is %s, got %s",
						       expected->fb ? "non-NULL" : "NULL",
						       actual->fb ? "non-NULL" : "NULL");
		ret = false;
	}

	STATE_CHECK_S32(ret, p, plane->name, expected, actual, crtc_x);
	STATE_CHECK_S32(ret, p, plane->name, expected, actual, crtc_y);
	STATE_CHECK_U32(ret, p, plane->name, expected, actual, crtc_w);
	STATE_CHECK_U32(ret, p, plane->name, expected, actual, crtc_h);
	STATE_CHECK_U32_16_16(ret, p, plane->name, expected, actual, src_x);
	STATE_CHECK_U32_16_16(ret, p, plane->name, expected, actual, src_y);
	STATE_CHECK_U32_16_16(ret, p, plane->name, expected, actual, src_w);
	STATE_CHECK_U32_16_16(ret, p, plane->name, expected, actual, src_h);
	STATE_CHECK_S32(ret, p, plane->name, expected, actual, hotspot_x);
	STATE_CHECK_S32(ret, p, plane->name, expected, actual, hotspot_y);
	STATE_CHECK_U16(ret, p, plane->name, expected, actual, alpha);
	STATE_CHECK_U16(ret, p, plane->name, expected, actual, pixel_blend_mode);
	STATE_CHECK_U32(ret, p, plane->name, expected, actual, rotation);
	STATE_CHECK_U32(ret, p, plane->name, expected, actual, zpos);
	STATE_CHECK_U32(ret, p, plane->name, expected, actual, normalized_zpos);
	STATE_CHECK_U32(ret, p, plane->name, expected, actual, color_encoding);
	STATE_CHECK_U32(ret, p, plane->name, expected, actual, color_range);

	// TODO: damage clips

	STATE_CHECK_BOOL(ret, p, plane->name, expected, actual, ignore_damage_clips);
	STATE_CHECK_S32(ret, p, plane->name, expected, actual, src.x1);
	STATE_CHECK_S32(ret, p, plane->name, expected, actual, src.x2);
	STATE_CHECK_S32(ret, p, plane->name, expected, actual, src.y1);
	STATE_CHECK_S32(ret, p, plane->name, expected, actual, src.y2);
	STATE_CHECK_S32(ret, p, plane->name, expected, actual, dst.x1);
	STATE_CHECK_S32(ret, p, plane->name, expected, actual, dst.x2);
	STATE_CHECK_S32(ret, p, plane->name, expected, actual, dst.y1);
	STATE_CHECK_S32(ret, p, plane->name, expected, actual, dst.y2);
	STATE_CHECK_BOOL(ret, p, plane->name, expected, actual, visible);
	STATE_CHECK_U32(ret, p, plane->name, expected, actual, scaling_filter);

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_plane_compare_state);

/**
 * drm_atomic_helper_crtc_compare_state - default &drm_crtc_funcs.atomic_sro_compare_state hook
 * @crtc: the CRTC being compared
 * @p: printer for reporting mismatches
 * @expected: the committed &struct drm_crtc_state
 * @actual: the &struct drm_crtc_state read out from hardware
 *
 * Compares the base &struct drm_crtc_state fields of @actual to
 * @expected and reports any mismatches through @p. Drivers subclassing
 * the CRTC state should call this first, then compare their own fields.
 *
 * RETURNS:
 *
 * True if the states are identical, false otherwise.
 */
bool drm_atomic_helper_crtc_compare_state(struct drm_crtc *crtc,
					  struct drm_printer *p,
					  struct drm_crtc_state *expected,
					  struct drm_crtc_state *actual)
{
	bool ret = true;

	STATE_CHECK_PTR(ret, p, crtc->name, expected, actual, crtc);
	STATE_CHECK_BOOL(ret, p, crtc->name, expected, actual, enable);
	STATE_CHECK_BOOL(ret, p, crtc->name, expected, actual, active);
	STATE_CHECK_BOOL(ret, p, crtc->name, expected, actual, no_vblank);
	STATE_CHECK_U32(ret, p, crtc->name, expected, actual, plane_mask);
	STATE_CHECK_U32(ret, p, crtc->name, expected, actual, connector_mask);
	STATE_CHECK_U32(ret, p, crtc->name, expected, actual, encoder_mask);

	STATE_CHECK_DISPLAY_MODE(ret, p, crtc->name, expected, actual, mode);
	STATE_CHECK_DISPLAY_MODE(ret, p, crtc->name, expected, actual, adjusted_mode);
	STATE_CHECK_PROPERTY_BLOB(ret, p, crtc->name, expected, actual, mode_blob);
	STATE_CHECK_PROPERTY_BLOB(ret, p, crtc->name, expected, actual, degamma_lut);
	STATE_CHECK_PROPERTY_BLOB(ret, p, crtc->name, expected, actual, ctm);
	STATE_CHECK_PROPERTY_BLOB(ret, p, crtc->name, expected, actual, gamma_lut);
	STATE_CHECK_BOOL(ret, p, crtc->name, expected, actual, vrr_enabled);
	STATE_CHECK_BOOL(ret, p, crtc->name, expected, actual, self_refresh_active);
	STATE_CHECK_U32(ret, p, crtc->name, expected, actual, scaling_filter);

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_crtc_compare_state);

/**
 * drm_atomic_helper_connector_compare_state - default &drm_connector_funcs.atomic_sro_compare_state hook
 * @conn: the connector being compared
 * @p: printer for reporting mismatches
 * @expected: the committed &struct drm_connector_state
 * @actual: the &struct drm_connector_state read out from hardware
 *
 * Compares the base &struct drm_connector_state fields of @actual to
 * @expected and reports any mismatches through @p. Drivers subclassing
 * the connector state should call this first, then compare their own
 * fields.
 *
 * RETURNS:
 *
 * True if the states are identical, false otherwise.
 */
bool drm_atomic_helper_connector_compare_state(struct drm_connector *conn,
					       struct drm_printer *p,
					       struct drm_connector_state *expected,
					       struct drm_connector_state *actual)
{
	bool ret = true;

	STATE_CHECK_PTR(ret, p, conn->name, expected, actual, connector);
	STATE_CHECK_PTR(ret, p, conn->name, expected, actual, crtc);
	STATE_CHECK_PTR(ret, p, conn->name, expected, actual, best_encoder);
	STATE_CHECK_U32(ret, p, conn->name, expected, actual, link_status);

	STATE_CHECK_U32(ret, p, conn->name, expected, actual, tv.select_subconnector);
	STATE_CHECK_U32(ret, p, conn->name, expected, actual, tv.subconnector);

	STATE_CHECK_BOOL(ret, p, conn->name, expected, actual, self_refresh_aware);
	STATE_CHECK_U32(ret, p, conn->name, expected, actual, picture_aspect_ratio);
	STATE_CHECK_U32(ret, p, conn->name, expected, actual, content_type);
	STATE_CHECK_U32(ret, p, conn->name, expected, actual, hdcp_content_type);
	STATE_CHECK_U32(ret, p, conn->name, expected, actual, scaling_mode);
	STATE_CHECK_U32(ret, p, conn->name, expected, actual, content_protection);
	STATE_CHECK_U32(ret, p, conn->name, expected, actual, colorspace);

	/*
	 * NOTE: We can't check max_bpc and max_requested_bpc because it
	 * will typically come from userspace and we can't read it out
	 * from the hardware.
	 */

	STATE_CHECK_U32(ret, p, conn->name, expected, actual, privacy_screen_sw_state);
	STATE_CHECK_PROPERTY_BLOB(ret, p, conn->name, expected, actual, hdr_output_metadata);

	STATE_CHECK_U32(ret, p, conn->name, expected, actual, hdmi.broadcast_rgb);
	STATE_CHECK_BOOL(ret, p, conn->name, expected, actual, hdmi.infoframes.avi.set);
	STATE_CHECK_INFOFRAME(ret, p, conn->name, expected, actual, hdmi.infoframes.avi.data);
	STATE_CHECK_BOOL(ret, p, conn->name, expected, actual, hdmi.infoframes.hdr_drm.set);
	STATE_CHECK_INFOFRAME(ret, p, conn->name, expected, actual, hdmi.infoframes.hdr_drm.data);
	STATE_CHECK_BOOL(ret, p, conn->name, expected, actual, hdmi.infoframes.spd.set);
	STATE_CHECK_INFOFRAME(ret, p, conn->name, expected, actual, hdmi.infoframes.spd.data);
	STATE_CHECK_BOOL(ret, p, conn->name, expected, actual, hdmi.infoframes.hdmi.set);
	STATE_CHECK_INFOFRAME(ret, p, conn->name, expected, actual, hdmi.infoframes.hdmi.data);
	STATE_CHECK_BOOL(ret, p, conn->name, expected, actual, hdmi.is_limited_range);
	STATE_CHECK_U32(ret, p, conn->name, expected, actual, hdmi.output_bpc);
	STATE_CHECK_U32(ret, p, conn->name, expected, actual, hdmi.output_format);
	STATE_CHECK_U64(ret, p, conn->name, expected, actual, hdmi.tmds_char_rate);

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_connector_compare_state);

/**
 * drm_atomic_helper_bridge_compare_state - default &drm_bridge_funcs.atomic_sro_compare_state hook
 * @bridge: the bridge being compared
 * @p: printer for reporting mismatches
 * @expected: the committed &struct drm_bridge_state
 * @actual: the &struct drm_bridge_state read out from hardware
 *
 * Compares the base &struct drm_bridge_state fields of @actual to
 * @expected and reports any mismatches through @p. Drivers subclassing
 * the bridge state should call this first, then compare their own
 * fields.
 *
 * RETURNS:
 *
 * True if the states are identical, false otherwise.
 */
bool drm_atomic_helper_bridge_compare_state(struct drm_bridge *bridge,
					    struct drm_printer *p,
					    struct drm_bridge_state *expected,
					    struct drm_bridge_state *actual)
{
	bool ret = true;

	STATE_CHECK_PTR(ret, p, bridge->name, expected, actual, bridge);
	STATE_CHECK_U32_X(ret, p, bridge->name, expected, actual, input_bus_cfg.format);
	STATE_CHECK_U32_X(ret, p, bridge->name, expected, actual, input_bus_cfg.flags);
	STATE_CHECK_U32_X(ret, p, bridge->name, expected, actual, output_bus_cfg.format);
	STATE_CHECK_U32_X(ret, p, bridge->name, expected, actual, output_bus_cfg.flags);

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_bridge_compare_state);

/**
 * drm_atomic_helper_print_state_mismatch - report a state comparison mismatch
 * @p: printer to report through
 * @name: human-readable name of the object (e.g. plane or CRTC name)
 * @field: name of the mismatching field
 * @format: printf-style format string describing the mismatch
 *
 * Helper used by atomic_sro_compare_state implementations and the
 * STATE_CHECK_* macros to report a field-level mismatch between the
 * committed state and the state read back from hardware.
 */
void __printf(4, 5)
drm_atomic_helper_print_state_mismatch(struct drm_printer *p,
				       const char *name,
				       const char *field,
				       const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	drm_printf(p, "%s configuration mismatch in %s %pV\n", name, field, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(drm_atomic_helper_print_state_mismatch);
