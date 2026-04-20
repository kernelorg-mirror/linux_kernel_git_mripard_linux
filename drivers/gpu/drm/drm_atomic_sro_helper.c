// SPDX-License-Identifier: GPL-2.0-or-later

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_sro.h>
#include <drm/drm_atomic_sro_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
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
