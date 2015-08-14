#include <string.h>

#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h> /* use xoverlay interface of playbin2 - need to look at concept of oop in C */

#include <gdk/gdk.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#else
#error "Unsupported platform"
#endif

/* structure to contain all player data, UI components */
typedef struct _CustomData {
	GstElement *playbin2; /* only pipeline */

	GtkWidget *slider; /* seeking, time update*/
	GtkWidget *streams_list; /* Text wiget to display stream information */
	gulong slider_update_signal_id; /* signal id for slider update signal */

	GstState state; /* Current state of pipeline */
	gint64 duration; /* total duration of clip */
} CustomData;

/* stream details table IDs*/
enum {
	COL_STREAM_NAME = 0,
	COL_STREAM_DETAILS,
	NUM_COLS
};

/*
 * @brief callback when GTK creates physical window
 *        retrive handle and provide to gstreamer through XOverlay interface
 */
static void realise_cb (GtkWidget *widget, CustomData *data) {
	GdkWindow *window = gtk_widget_get_window (widget);
	guintptr window_handle;

	/* why so serius? */
	if (!gdk_window_ensure_native (window)) {
		g_error ("couldn't create native window needed for GstXOverlay!");
	}

	/* get window handle */
#ifdef GDK_WINDOWING_X11
	window_handle = GDK_WINDOW_XID(window);
#else
#error "Unsupported platform!!"
#endif
	/* pass window to xoverlay of playbin2 */
	gst_x_overlay_set_window_handle(GST_X_OVERLAY (data->playbin2), window_handle);
}

/*
 * @brief callback when PLAY button is hit
 * */
static void play_cb(GtkButton *button, CustomData *data) {
	gst_element_set_state(data->playbin2, GST_STATE_PLAYING);
}

/*
 * @brief callback when PAUSE button is hit
 * */
static void pause_cb(GtkButton *button, CustomData *data) {
	gst_element_set_state(data->playbin2, GST_STATE_PAUSED);
}

/*
 * @brief callback when STOP button is hit
 * */
static void stop_cb(GtkButton *button, CustomData *data) {
	gst_element_set_state(data->playbin2, GST_STATE_READY);
}

/* @brief update stream of playbin2 based on stream name*/
static inline void stream_set(gchar* stream_name, CustomData *data) {
	gchar *o_brace, *c_brace;
	gchar *tstr, *property;
	gint tint, id;
	o_brace = g_strstr_len(stream_name, strlen(stream_name), "[");
	c_brace = g_strstr_len(stream_name, strlen(stream_name), "]");
	tint = c_brace - o_brace;
	tstr = g_new(gchar, tint);
	g_strlcpy(tstr, o_brace + 1, tint);
	id = atoi(tstr);
	g_free(tstr);
	*o_brace = '\0';
	tstr = g_ascii_strdown(stream_name, strlen(stream_name));
	property = g_strdup_printf("current-%s", tstr);
	g_free(tstr);

	g_object_get(data->playbin2, property, &tint, NULL);
	if (tint != id) {
		g_print("Selecting %s : %d.\n", property, id);
		g_object_set(data->playbin2, property, id, NULL);
	}
	g_free(property);
}

/* @brief callback when tree view is double-clicked : select corrosponding stream*/
static void stream_select_cb(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn* col, CustomData *data) {
	GtkTreeModel *model;
	GtkTreeIter iter;

	model = gtk_tree_view_get_model(tree_view);
	if (gtk_tree_model_get_iter(model, &iter, path)) {
		gchar *stream_name;
		gtk_tree_model_get(model, &iter, COL_STREAM_NAME, &stream_name, -1);

		g_print("Double clicked row : %s\n", stream_name);
		stream_set(stream_name, data);
		g_free(stream_name);
	}
}

/*
 * @brief callback when main window is closed
 * */
static void delete_event_cb (GtkWidget *widget, GdkEvent *event, CustomData *data){
	stop_cb(NULL, data); /* stop playback*/
	gtk_main_quit(); /* huh, what?? */
}

/*
 * @brief callback when window is redrawn
 * 	reasons -> window damage, exposure, rescaling etc
 * 	playback handling is taken care of by GStreamer
 * 	Just draw a black rectangle to avoid any garbage showing in window after redraw
 * */
static gboolean expose_cb(GtkWidget* widget, GdkEventExpose *event, CustomData* data) {
	if (data->state < GST_STATE_PAUSED) {
		GtkAllocation allocation;
		GdkWindow *window = gtk_widget_get_window(widget);
		cairo_t *cr; /* huh, what?? */

		/* Cairo is 2D graphics library which will be used to clear window
		 * its anyways a gstreamer dependency, so it will be available */
		gtk_widget_get_allocation(widget, &allocation); /* whats gtk allocation? */
		cr = gdk_cairo_create(window);
		cairo_set_source_rgb(cr, 0, 0, 0); /* I suppose a black base color for cairo object */
		cairo_rectangle(cr, 0, 0, allocation.width, allocation.height); /* draw */
		cairo_fill(cr); /* fill with base color */
		cairo_destroy(cr);
	}
	return FALSE;
}

/*
 * @brief callback when slider changes its position, perform seek
 * */
static void slider_cb(GtkRange *range, CustomData *data) {
	gdouble value = gtk_range_get_value(GTK_RANGE(data->slider));
	gst_element_seek_simple(data->playbin2, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
			(gint64)(value * GST_SECOND));
}

/*
 * @brief creates all GTK+ widgets that compose the player & sets up callbacks
 * */
static void create_ui(CustomData *data) {
	GtkWidget *main_window; /* uppermost container window */
	GtkWidget *video_window; /* video is shown here */
	GtkWidget *main_box; /* holds hbox & controls */
	GtkWidget *main_hbox; /* hold video window & streaminfo widget */
	GtkWidget *controls; /* hold buttons & slider */
	GtkWidget *play_button, *pause_button, *stop_button;
	GtkCellRenderer     *renderer;

	main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(G_OBJECT(main_window), "delete-event", G_CALLBACK(delete_event_cb), data);

	video_window = gtk_drawing_area_new();
	gtk_widget_set_double_buffered(video_window, FALSE);
	g_signal_connect(G_OBJECT(video_window), "realize", G_CALLBACK(realise_cb), data);
	g_signal_connect (video_window, "expose_event", G_CALLBACK (expose_cb), data);

	play_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_PLAY);
	g_signal_connect (G_OBJECT (play_button), "clicked", G_CALLBACK (play_cb), data);

	pause_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_PAUSE);
	g_signal_connect (G_OBJECT (pause_button), "clicked", G_CALLBACK (pause_cb), data);

	stop_button = gtk_button_new_from_stock (GTK_STOCK_MEDIA_STOP);
	g_signal_connect (G_OBJECT (stop_button), "clicked", G_CALLBACK (stop_cb), data);

	data->slider = gtk_hscale_new_with_range(0, 100, 1);
	gtk_scale_set_draw_value(GTK_SCALE(data->slider), 0);
	data->slider_update_signal_id = g_signal_connect (G_OBJECT (data->slider), "value-changed", G_CALLBACK (slider_cb), data);

	data->streams_list = gtk_tree_view_new();
	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_renderer_set_fixed_size(renderer, 15, -1);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(data->streams_list),
			-1, "Stream Name", renderer, "text", COL_STREAM_NAME, NULL);
	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_renderer_set_fixed_size(renderer, 15, -1);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(data->streams_list),
			-1, "Stream Details", renderer, "text", COL_STREAM_DETAILS, NULL);
	g_signal_connect(data->streams_list, "row-activated", G_CALLBACK(stream_select_cb), data);
	//gtk_text_view_set_editable (GTK_TEXT_VIEW (data->streams_list), FALSE);

	controls = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (controls), play_button, FALSE, FALSE, 2);
	gtk_box_pack_start (GTK_BOX (controls), pause_button, FALSE, FALSE, 2);
	gtk_box_pack_start (GTK_BOX (controls), stop_button, FALSE, FALSE, 2);
	gtk_box_pack_start (GTK_BOX (controls), data->slider, TRUE, TRUE, 2);

	main_box = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (main_box), video_window, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (main_box), data->streams_list, FALSE, FALSE, 2);
	gtk_box_pack_start (GTK_BOX (main_box), controls, FALSE, FALSE, 0);
	gtk_container_add (GTK_CONTAINER (main_window), main_box);
	gtk_window_set_default_size (GTK_WINDOW (main_window), 640, 480);

	gtk_widget_show_all (main_window);
}

/* This function is called periodically to refresh the GUI */
static gboolean refresh_ui (CustomData *data) {
	GstFormat fmt = GST_FORMAT_TIME;
	gint64 current = -1;

	/* We do not want to update anything unless we are in the PAUSED or PLAYING states */
	if (data->state < GST_STATE_PAUSED)
		return TRUE;

	/* If we didn't know it yet, query the stream duration */
	if (!GST_CLOCK_TIME_IS_VALID (data->duration)) {
		if (!gst_element_query_duration (data->playbin2, &fmt, &data->duration)) {
			g_printerr ("Could not query current duration.\n");
		} else {
			/* Set the range of the slider to the clip duration, in SECONDS */
			gtk_range_set_range (GTK_RANGE (data->slider), 0, (gdouble)data->duration / GST_SECOND);
		}
	}

	if (gst_element_query_position (data->playbin2, &fmt, &current)) {
		/* Block the "value-changed" signal, so the slider_cb function is not called
		 *      * (which would trigger a seek the user has not requested) */
		g_signal_handler_block (data->slider, data->slider_update_signal_id);
		/* Set the position of the slider to the current pipeline positoin, in SECONDS */
		gtk_range_set_value (GTK_RANGE (data->slider), (gdouble)current / GST_SECOND);
		/* Re-enable the signal */
		g_signal_handler_unblock (data->slider, data->slider_update_signal_id);
	}
	return TRUE;
}

/* This function is called when new metadata is discovered in the stream */
static void tags_cb (GstElement *playbin2, gint stream, CustomData *data) {
	/* We are possibly in a GStreamer working thread, so we notify the main
	 *    * thread of this event through a message in the bus */
	gst_element_post_message (playbin2,
			gst_message_new_application (GST_OBJECT (playbin2),
				gst_structure_new ("tags-changed", NULL)));
}

/* This function is called when an error message is posted on the bus */
static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
	GError *err;
	gchar *debug_info;

	/* Print error details on the screen */
	gst_message_parse_error (msg, &err, &debug_info);
	g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
	g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
	g_clear_error (&err);
	g_free (debug_info);

	/* Set the pipeline to READY (which stops playback) */
	gst_element_set_state (data->playbin2, GST_STATE_READY);
}

/* This function is called when an End-Of-Stream message is posted on the bus.
 *  * We just set the pipeline to READY (which stops playback) */
static void eos_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
	g_print ("End-Of-Stream reached.\n");
	gst_element_set_state (data->playbin2, GST_STATE_READY);
}

/* This function is called when the pipeline changes states. We use it to
 *  * keep track of the current state. */
static void state_changed_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
	GstState old_state, new_state, pending_state;
	gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
	if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->playbin2)) {
		data->state = new_state;
		g_print ("State set to %s\n", gst_element_state_get_name (new_state));
		if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
			/* For extra responsiveness, we refresh the GUI as soon as we reach the PAUSED state */
			refresh_ui (data);
		}
	}
}

/* Extract metadata from all the streams and write it to the text widget in the GUI */
static void analyze_streams (CustomData *data) {
	gint i;
	GstTagList *tags;
	gchar *str_name, *str_details;
	guint rate;
	gint n_video, n_audio, n_text;

	GtkListStore *store;
	GtkTreeIter iter;

	store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_STRING);
	/* Read some properties */
	g_object_get (data->playbin2, "n-video", &n_video, NULL);
	g_object_get (data->playbin2, "n-audio", &n_audio, NULL);
	g_object_get (data->playbin2, "n-text", &n_text, NULL);

	for (i = 0; i < n_video; i++) {
		tags = NULL;
		/* Retrieve the stream's video tags */
		g_signal_emit_by_name (data->playbin2, "get-video-tags", i, &tags);
		if (tags) {
			str_name = g_strdup_printf("VIDEO[%d]", i);
			str_details = gst_tag_list_to_string (tags);
			/*Fill stream details*/
			gtk_list_store_append (store, &iter);
			gtk_list_store_set (store, &iter,
					COL_STREAM_NAME, str_name,
					COL_STREAM_DETAILS, str_details,
					-1);
			g_free(str_name);
			g_free(str_details);
			gst_tag_list_free (tags);
		}
	}

	for (i = 0; i < n_audio; i++) {
		tags = NULL;
		/* Retrieve the stream's audio tags */
		g_signal_emit_by_name (data->playbin2, "get-audio-tags", i, &tags);
		if (tags) {
			str_name = g_strdup_printf("AUDIO[%d]", i);
			str_details = gst_tag_list_to_string (tags);
			/*Fill stream details*/
			gtk_list_store_append (store, &iter);
			gtk_list_store_set (store, &iter,
					COL_STREAM_NAME, str_name,
					COL_STREAM_DETAILS, str_details,
					-1);
			g_free(str_name);
			g_free(str_details);
			gst_tag_list_free (tags);
		}
	}

	for (i = 0; i < n_text; i++) {
		tags = NULL;
		/* Retrieve the stream's subtitle tags */
		g_signal_emit_by_name (data->playbin2, "get-text-tags", i, &tags);
		if (tags) {
			str_name = g_strdup_printf("TEXT[%d]", i);
			str_details = gst_tag_list_to_string (tags);
			/*Fill stream details*/
			gtk_list_store_append (store, &iter);
			gtk_list_store_set (store, &iter,
					COL_STREAM_NAME, str_name,
					COL_STREAM_DETAILS, str_details,
					-1);
			g_free(str_name);
			g_free(str_details);
			gst_tag_list_free (tags);
		}
	}
	GtkTreeModel *tree = GTK_TREE_MODEL (store);
	gtk_tree_view_set_model (GTK_TREE_VIEW (data->streams_list), tree);
	g_object_unref (tree);

}

/* This function is called when an "application" message is posted on the bus.
 *  * Here we retrieve the message posted by the tags_cb callback */
static void application_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
	if (g_strcmp0 (gst_structure_get_name (msg->structure), "tags-changed") == 0) {
		/* If the message is the "tags-changed" (only one we are currently issuing), update
		 *      * the stream info GUI */
		analyze_streams (data);
	}
}

int main(int argc, char *argv[]) {
	CustomData data;
	GstStateChangeReturn ret;
	GstBus *bus;

	/* Initialize GTK */
	gtk_init (&argc, &argv);

	/* Initialize GStreamer */
	gst_init (&argc, &argv);

	/* Initialize our data structure */
	memset (&data, 0, sizeof (data));
	data.duration = GST_CLOCK_TIME_NONE;

	/* Create the elements */
	data.playbin2 = gst_element_factory_make ("playbin2", "playbin2");

	if (!data.playbin2) {
		g_printerr ("Not all elements could be created.\n");
		return -1;
	}

	/* Set the URI to play */
	g_object_set (data.playbin2, "uri", "http://docs.gstreamer.com/media/sintel_cropped_multilingual.webm", NULL);

	/* Connect to interesting signals in playbin2 */
	g_signal_connect (G_OBJECT (data.playbin2), "video-tags-changed", (GCallback) tags_cb, &data);
	g_signal_connect (G_OBJECT (data.playbin2), "audio-tags-changed", (GCallback) tags_cb, &data);
	g_signal_connect (G_OBJECT (data.playbin2), "text-tags-changed", (GCallback) tags_cb, &data);

	/* Create the GUI */
	create_ui (&data);

	/* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
	bus = gst_element_get_bus (data.playbin2);
	gst_bus_add_signal_watch (bus);
	g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, &data);
	g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, &data);
	g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, &data);
	g_signal_connect (G_OBJECT (bus), "message::application", (GCallback)application_cb, &data);
	gst_object_unref (bus);

	/* Start playing */
	ret = gst_element_set_state (data.playbin2, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the pipeline to the playing state.\n");
		gst_object_unref (data.playbin2);
		return -1;
	}

	/* Register a function that GLib will call every second */
	g_timeout_add_seconds (1, (GSourceFunc)refresh_ui, &data);

	/* Start the GTK main loop. We will not regain control until gtk_main_quit is called. */
	gtk_main ();

	/* Free resources */
	gst_element_set_state (data.playbin2, GST_STATE_NULL);
	gst_object_unref (data.playbin2);
	return 0;
}
