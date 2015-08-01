#include <gst/gst.h>

int main(int argc, char* argv[]) {
    GstElement *pipeline, *source, *sink;
    GstBus *bus;
    GstMessage *msg;
    GstStateChangeReturn ret;

    /* initialize gstreamer */
    gst_init(&argc, &argv);

    /* create elements */
    source = gst_element_factory_make("videotestsrc", "source");
    sink = gst_element_factory_make("autovideosink", "sink");

    /* create pipeline */
    pipeline = gst_pipeline_new("test-pipeline");

    if (!pipeline || !source || !sink) {
        g_printerr("All elements are not created\n");
        return -1;
    }

    /* Build pipeline */
    gst_bin_add_many(GST_BIN(pipeline), source, sink, NULL);
    if (gst_element_link(source, sink) != TRUE) {
        g_printerr("Element could not be linked.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    /* set source properties */
    g_object_set(source, "pattern", 0, NULL);

    /* start playing */
    ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to start playback.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    /* wait till error or eos */
    bus = gst_element_get_bus(pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Parse message */
    if (msg != NULL) {
        GError *err;
        gchar *debug_info;

        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR:
                gst_message_parse_error(msg, &err, &debug_info);
                g_printerr("Error received from element %s : %s\n", GST_OBJECT_NAME(msg->src), err->message);
                g_printerr("Debugging info : %s\n", debug_info ? debug_info : "none");
                g_clear_error(&err);
                g_print("##2");
                g_free(debug_info);
                g_print("##3");
                break;
            case GST_MESSAGE_EOS:
                g_print("End of stream reached!\n");
                break;
            default:
                /*not possible still! */
                g_printerr("Unexpected message received");
                break;
        }
        g_print("##1");
        gst_message_unref(msg);
    }
    g_print("##4");
    /* Free elements */
    gst_object_unref(bus);
    g_print("##5");
    gst_element_set_state(pipeline, GST_STATE_NULL);
    g_print("##6");
    gst_object_unref(pipeline);
    return 0;
}
