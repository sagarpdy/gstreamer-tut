#include <gst/gst.h>

/* callback private date */
typedef struct _CustomData {
	GstElement *pipeline;
	GstElement *source;

	/* Audio SinkMap */
	GstElement *aconvert;
	GstElement *asink;

	/* Video SinkMap*/
	GstElement *vconvert;
	GstElement *vsink;
}CustomData;

/*callback handler*/
static void pad_added_handler(GstElement *src, GstPad *pad, CustomData *data);

int main(int argc, char* argv[]) {
	CustomData data;
	GstBus *bus;
	GstMessage *msg;
	GstStateChangeReturn ret;
	gboolean terminate = FALSE;

	/*Initialize gstreamer*/
	gst_init(&argc, &argv);

	/*create elements*/
	data.source = gst_element_factory_make ("uridecodebin", 0);
	data.aconvert = gst_element_factory_make("audioconvert", "aconvert");
	data.asink = gst_element_factory_make("autoaudiosink", "asink");
	data.vconvert = gst_element_factory_make("ffmpegcolorspace", "vconvert");
	data.vsink = gst_element_factory_make("autovideosink", "vsink");

	/*and empty pipeline*/
	data.pipeline = gst_pipeline_new("test-pipeline");

	if (!data.pipeline || !data.source || !data.aconvert || !data.asink) { 
		g_printerr("Elements couldn't be created\n");
		return -1;
	}

	if (!data.vconvert || !data.vsink) {
		g_printerr("VElements couldn't be created\n");
		return -1;
	}

	/* Build pipeline, keep source to be linked later with rest of pipe
	 * link only rest of bins */
	gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.aconvert, data.asink, data.vconvert, data.vsink, NULL);
	if (!gst_element_link(data.aconvert, data.asink) || !gst_element_link(data.vconvert, data.vsink)) {
		g_printerr("Element couldn't be linked");
		gst_object_unref(data.pipeline);
		return -1;
	}

	/* set URI to play */
	g_object_set(data.source, "uri", "http://docs.gstreamer.com/media/sintel_trailer-480p.webm", NULL);

	/* connect the pad_add handler to source */
	g_signal_connect(data.source, "pad-added", G_CALLBACK(pad_added_handler), &data);

	/* start playback so that demux, pad adding and then actual playback happens*/
	ret = gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr("Pipeline couldn't be set in playing state");
		gst_object_unref(data.pipeline);
		return -1;
	}

	/*Listen to bus and log status*/
	bus = gst_element_get_bus(data.pipeline);
	do {
						GstState old_state, new_state, pending_state;
		msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

		/* parse message */
		if (msg != NULL) {
			GError* err;
			gchar* debug_info;

			switch (GST_MESSAGE_TYPE(msg)) {
				case GST_MESSAGE_STATE_CHANGED:
					/*Print state change message from pipeline only - for now*/
//					if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data.pipeline)) {
						gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
						g_print("%s\tstate changed %s -> %s:\n",GST_MESSAGE_SRC_NAME(msg), gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
//					}

					break;
				case GST_MESSAGE_ERROR:
					gst_message_parse_error(msg, &err, &debug_info);
					g_printerr("Error received from element %s : %s\n", GST_OBJECT_NAME(msg->src), err->message);
					g_printerr("Debugging info : %s", debug_info ? debug_info : "none");
					g_clear_error(&err);
					g_free(debug_info);
					terminate = TRUE;
					break;
				case GST_MESSAGE_EOS:
					g_print("End-of-stream reached.\n");
					terminate = TRUE;
					break;
				default:
					g_printerr("Unexpected message, shouldn't be here!");
					break;
			}
		}
	}while (!terminate);

	/*Free resources*/
	gst_object_unref(bus);
	gst_element_set_state(data.pipeline, GST_STATE_NULL);
	gst_object_unref(data.pipeline);
	return 0;
}

/* callback impl*/
static void pad_added_handler(GstElement *src, GstPad *new_pad, CustomData *data) {
	GstPad *sink_pad = NULL; 
	GstPadLinkReturn ret;
	GstCaps *new_pad_caps = NULL;
	GstStructure *new_pad_struct = NULL;
	const gchar *new_pad_type = NULL;

	g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));


	/* check pad type*/
	new_pad_caps = gst_pad_get_caps(new_pad);
	new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
	new_pad_type = gst_structure_get_name(new_pad_struct);
	if (g_str_has_prefix(new_pad_type, "audio/x-raw")) {
		sink_pad = gst_element_get_static_pad (data->aconvert, "sink");
	} else if (g_str_has_prefix(new_pad_type, "video/x-raw")) {
		sink_pad = gst_element_get_static_pad (data->vconvert, "sink");
	} else {
		g_print(" It has type '%s' which is not raw audio/video. Ignore!\n", new_pad_type);
		goto exit;
	}

	/* If our converter is already linked, exit */
	if (gst_pad_is_linked(sink_pad)) {
		g_print(" We are already linked, Ignore!\n");
		goto exit;
	}

	/* Attempt the link */
	ret = gst_pad_link(new_pad, sink_pad);
	if (GST_PAD_LINK_FAILED(ret)) {
		g_print(" Type is '%s' link failed.\n", new_pad_type);
	} else {
		g_print(" Successfully linked (type '%s').\n", new_pad_type);
	}

exit:
	/*unreferance the new pad caps*/
	if (new_pad_caps != NULL) {
		gst_caps_unref(new_pad_caps);
	}
	gst_object_unref(sink_pad);
}
