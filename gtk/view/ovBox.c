/* *************************************************************************
 * Copyright (c) 2005 VMware, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * *************************************************************************/

/*
 * ovBox.c --
 *
 *      Implementation of a GTK+ overlapping box. Allows you to display and
 *      quickly move a child that overlaps another child.
 *
 *      Implementation notes
 *      --------------------
 *
 *      Changing 'fraction' is fast (we just move the 'overWin' X window, which
 *      ultimately copies a rectangle on the X server side), and does not
 *      flicker (the 'under' and 'over' GTK children are not re-drawn, except
 *      for parts of them that become exposed).
 *
 *      o Initially, we thought it could be done with only 2 X windows
 *
 *        Layout                  Hierarchy
 *        ------                  ---------
 *
 *          /- overWin --\        underWin
 *          |            |           overWin
 *        /-+- underWin -+-\
 *        | |            | |
 *        | \------------/ |
 *        |                |
 *        \----------------/
 *
 *        But the 'under' GTK child could create other X windows inside
 *        'underWin', which makes it impossible to guarantee that 'overWin'
 *        will stay stacked on top.
 *
 *      o So we are forced to use 3 X windows
 *
 *        Layout                  Hierarchy
 *        ------                  ---------
 *
 *            /- overWin --\      window
 *            |            |         overWin
 *        /---+- window ---+---\     underWin
 *        |   |            |   |
 *        | /-+- underWin -+-\ |
 *        | | |            | | |
 *        | | \------------/ | |
 *        | |                | |
 *        | \----------------/ |
 *        |                    |
 *        \--------------------/
 *
 *  --hpreg
 */


#include "gtk/view/ovBox.h"


struct _ViewOvBoxPrivate
{
   GdkWindow *underWin;
   GtkWidget *under;
   GdkWindow *overWin;
   GtkWidget *over;
   GtkRequisition overR;
   unsigned int min;
   double fraction;
   gint verticalOffset;
};

#define VIEW_OV_BOX_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), VIEW_TYPE_OV_BOX, ViewOvBoxPrivate))

/* The unaltered parent class. */
static GtkBoxClass *parentClass;


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxInit --
 *
 *      Initialize a ViewOvBox.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
ViewOvBoxInit(GTypeInstance *instance, // IN
              gpointer klass)          // Unused
{
   ViewOvBox *that;
   ViewOvBoxPrivate *priv;

   that = VIEW_OV_BOX(instance);
   that->priv = VIEW_OV_BOX_GET_PRIVATE(that);
   priv = that->priv;

   GTK_WIDGET_UNSET_FLAGS(that, GTK_NO_WINDOW);

   priv->underWin = NULL;
   priv->under = NULL;
   priv->overWin = NULL;
   priv->over = NULL;
   priv->overR.height = -1;
   priv->overR.width = -1;
   priv->min = 0;
   priv->fraction = 0;
   priv->verticalOffset = 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxMap --
 *
 *      "map" method of a ViewOvBox.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
ViewOvBoxMap(GtkWidget *widget) // IN
{
   gdk_window_show(widget->window);
   GTK_WIDGET_CLASS(parentClass)->map(widget);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxUnmap --
 *
 *      "unmap" method of a ViewOvBox.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
ViewOvBoxUnmap(GtkWidget *widget) // IN
{
   gdk_window_hide(widget->window);
   GTK_WIDGET_CLASS(parentClass)->unmap(widget);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxGetActualMin --
 *
 *      Retrieve the actual 'min' value, i.e. a value that is guaranteed not to
 *      exceed the height of the 'over' child.
 *
 * Results:
 *      The actual 'min' value.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static inline unsigned int
ViewOvBoxGetActualMin(ViewOvBox *that) // IN
{
   return MIN(that->priv->min, that->priv->overR.height);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxGetUnderGeometry --
 *
 *      Retrieve the geometry to apply to 'that->underWin'.
 *
 * Results:
 *      The geometry
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
ViewOvBoxGetUnderGeometry(ViewOvBox *that, // IN
                          int *x,          // OUT
                          int *y,          // OUT
                          int *width,      // OUT
                          int *height)     // OUT
{
   unsigned int min;
   GtkAllocation *allocation;

   min = ViewOvBoxGetActualMin(that);
   allocation = &GTK_WIDGET(that)->allocation;

   *x = 0;
   *y = min;
   *width = allocation->width;
   *height = allocation->height - min;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxGetOverGeometry --
 *
 *      Retrieve the geometry to apply to 'that->overWin'.
 *
 * Results:
 *      The geometry
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
ViewOvBoxGetOverGeometry(ViewOvBox *that, // IN
                         int *x,          // OUT
                         int *y,          // OUT
                         int *width,      // OUT
                         int *height)     // OUT
{
   ViewOvBoxPrivate *priv;
   gboolean expand;
   gboolean fill;
   guint padding;
   unsigned int boxWidth;

   priv = that->priv;

   if (priv->over) {
      /*
       * When a child's expand or fill property changes, GtkBox queues
       * a resize for the child.
       */
      gtk_container_child_get(GTK_CONTAINER(that), priv->over,
                              "expand", &expand,
                              "fill", &fill,
                              "padding", &padding,
                              NULL);
   } else {
      /* Default values used by GtkBox. */
      expand = TRUE;
      fill = TRUE;
      padding = 0;
   }

   boxWidth = GTK_WIDGET(that)->allocation.width;
   if (!expand) {
      *width = MIN(priv->overR.width, boxWidth - padding);
      *x = padding;
   } else if (!fill) {
      *width = MIN(priv->overR.width, boxWidth);
      *x = (boxWidth - *width) / 2;
   } else {
      *width = boxWidth;
      *x = 0;
   }

   *y =   (priv->overR.height - ViewOvBoxGetActualMin(that))
        * (priv->fraction - 1) + priv->verticalOffset;
   *height = priv->overR.height;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxSetBackground --
 *
 *      Set the background color of the 'underWin' and 'overWin' X windows.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
ViewOvBoxSetBackground(ViewOvBox *that) // IN
{
   GtkWidget *widget;

   widget = GTK_WIDGET(that);
   gtk_style_set_background(widget->style, widget->window, GTK_STATE_NORMAL);
   gtk_style_set_background(widget->style, that->priv->underWin, GTK_STATE_NORMAL);
   gtk_style_set_background(widget->style, that->priv->overWin, GTK_STATE_NORMAL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxRealize --
 *
 *      "realize" method of a ViewOvBox.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
ViewOvBoxRealize(GtkWidget *widget) // IN
{
   ViewOvBox *that;
   ViewOvBoxPrivate *priv;
   GdkWindowAttr attributes;
   gint mask;
   GtkAllocation *allocation;

   GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);

   that = VIEW_OV_BOX(widget);
   priv = that->priv;

   attributes.window_type = GDK_WINDOW_CHILD;
   attributes.wclass = GDK_INPUT_OUTPUT;
   attributes.visual = gtk_widget_get_visual(widget);
   attributes.colormap = gtk_widget_get_colormap(widget);
   attributes.event_mask = gtk_widget_get_events(widget) | GDK_EXPOSURE_MASK;
   mask = GDK_WA_VISUAL | GDK_WA_COLORMAP | GDK_WA_X | GDK_WA_Y;

   allocation = &widget->allocation;
   attributes.x = allocation->x;
   attributes.y = allocation->y;
   attributes.width = allocation->width;
   attributes.height = allocation->height;
   widget->window = gdk_window_new(gtk_widget_get_parent_window(widget),
				   &attributes, mask);
   gdk_window_set_user_data(widget->window, that);
   widget->style = gtk_style_attach(widget->style, widget->window);

   /*
    * The order in which we create the children X window matters: the child
    * created last is stacked on top. --hpreg
    */

   ViewOvBoxGetUnderGeometry(that, &attributes.x, &attributes.y,
                             &attributes.width, &attributes.height);
   priv->underWin = gdk_window_new(widget->window, &attributes, mask);
   gdk_window_set_user_data(priv->underWin, that);
   if (priv->under) {
      gtk_widget_set_parent_window(priv->under, priv->underWin);
   }
   gdk_window_show(priv->underWin);

   ViewOvBoxGetOverGeometry(that, &attributes.x, &attributes.y,
                            &attributes.width, &attributes.height);
   priv->overWin = gdk_window_new(widget->window, &attributes, mask);
   gdk_window_set_user_data(priv->overWin, that);
   if (priv->over) {
      gtk_widget_set_parent_window(priv->over, priv->overWin);
   }
   gdk_window_show(priv->overWin);

   ViewOvBoxSetBackground(that);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxUnrealize --
 *
 *      "unrealize" method of a ViewOvBox.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
ViewOvBoxUnrealize(GtkWidget *widget) // IN
{
   ViewOvBox *that;
   ViewOvBoxPrivate *priv;

   that = VIEW_OV_BOX(widget);
   priv = that->priv;

   /*
    * Unrealize the parent before destroying the windows so that we end up
    * unrealizing all the child widgets before destroying the child windows,
    * giving them a chance to reparent their windows before we clobber them.
    */
   GTK_WIDGET_CLASS(parentClass)->unrealize(widget);


   gdk_window_set_user_data(priv->underWin, NULL);
   gdk_window_destroy(priv->underWin);
   priv->underWin = NULL;

   gdk_window_set_user_data(priv->overWin, NULL);
   gdk_window_destroy(priv->overWin);
   priv->overWin = NULL;

}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxSizeRequest --
 *
 *      "size_request" method of a ViewOvBox.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
ViewOvBoxSizeRequest(GtkWidget *widget,           // IN
                     GtkRequisition *requisition) // OUT
{
   ViewOvBox *that;
   ViewOvBoxPrivate *priv;
   GtkRequisition underR;
   gboolean expand;
   gboolean fill;
   guint padding;
   unsigned int min;

   that = VIEW_OV_BOX(widget);
   priv = that->priv;

   gtk_widget_size_request(priv->under, &underR);
   gtk_widget_size_request(priv->over, &priv->overR);

   gtk_container_child_get(GTK_CONTAINER(that), priv->over,
                           "expand", &expand,
                           "fill", &fill,
                           "padding", &padding,
                           NULL);
   requisition->width = MAX(underR.width,
                            priv->overR.width + ((expand || fill) ? 0 : padding));
   min = ViewOvBoxGetActualMin(that);
   requisition->height = MAX(underR.height + min, priv->overR.height);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxSizeAllocate --
 *
 *      "size_allocate" method of a ViewOvBox.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
ViewOvBoxSizeAllocate(GtkWidget *widget,         // IN
                      GtkAllocation *allocation) // IN
{
   ViewOvBox *that;
   ViewOvBoxPrivate *priv;
   GtkAllocation under;
   GtkAllocation over;

   widget->allocation = *allocation;

   that = VIEW_OV_BOX(widget);
   priv = that->priv;

   ViewOvBoxGetUnderGeometry(that, &under.x, &under.y, &under.width,
                             &under.height);
   ViewOvBoxGetOverGeometry(that, &over.x, &over.y, &over.width, &over.height);

   if (GTK_WIDGET_REALIZED(widget)) {
      gdk_window_move_resize(widget->window, allocation->x, allocation->y,
                             allocation->width, allocation->height);
      gdk_window_move_resize(priv->underWin, under.x, under.y, under.width,
                             under.height);
      gdk_window_move_resize(priv->overWin, over.x, over.y, over.width,
                             over.height);
   }

   under.x = 0;
   under.y = 0;
   gtk_widget_size_allocate(priv->under, &under);
   over.x = 0;
   over.y = 0;
   gtk_widget_size_allocate(priv->over, &over);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxStyleSet --
 *
 *      "style_set" method of a ViewOvBox.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
ViewOvBoxStyleSet(GtkWidget *widget,       // IN
                  GtkStyle *previousStyle) // IN: Unused
{
   ViewOvBox *that;

   that = VIEW_OV_BOX(widget);

   if (GTK_WIDGET_REALIZED(widget)) {
      ViewOvBoxSetBackground(that);
   }

   GTK_WIDGET_CLASS(parentClass)->style_set(widget, previousStyle);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxSetChild --
 *
 *      Set a child of a ViewOvBox.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
ViewOvBoxSetChild(ViewOvBox *that,     // IN
                  GtkWidget **child,   // IN
                  GdkWindow *childWin, // IN
                  GtkWidget *widget)   // IN
{
   GtkWidget *oldChild = *child;

   if (oldChild) {
      g_object_ref(oldChild);
      gtk_container_remove(GTK_CONTAINER(that), oldChild);
   }

   *child = widget;
   if (*child) {
      gtk_widget_set_parent_window(widget, childWin);
      gtk_container_add(GTK_CONTAINER(that), *child);
   }

   if (oldChild) {
      g_object_unref(oldChild);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxSetOver --
 *
 *      Base implementation of ViewOvBox_SetOver.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
ViewOvBoxSetOver(ViewOvBox *that,   // IN
                 GtkWidget *widget) // IN
{
   ViewOvBoxSetChild(that, &that->priv->over, that->priv->overWin, widget);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBoxClassInit --
 *
 *      Initialize the ViewOvBoxClass.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
ViewOvBoxClassInit(ViewOvBoxClass *klass) // IN
{
   GObjectClass *objectClass;
   GtkWidgetClass *widgetClass;

   objectClass = G_OBJECT_CLASS(klass);
   widgetClass = GTK_WIDGET_CLASS(klass);

   widgetClass->map = ViewOvBoxMap;
   widgetClass->unmap = ViewOvBoxUnmap;
   widgetClass->realize = ViewOvBoxRealize;
   widgetClass->unrealize = ViewOvBoxUnrealize;
   widgetClass->size_request = ViewOvBoxSizeRequest;
   widgetClass->size_allocate = ViewOvBoxSizeAllocate;
   widgetClass->style_set = ViewOvBoxStyleSet;

   klass->set_over = ViewOvBoxSetOver;

   parentClass = g_type_class_peek_parent(klass);

   g_type_class_add_private(objectClass, sizeof(ViewOvBoxPrivate));
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBox_GetType --
 *
 *      Get the (memoized) GType of the ViewOvBox GTK+ object.
 *
 * Results:
 *      The GType
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

GType
ViewOvBox_GetType(void)
{
   static GType type = 0;

   if (type == 0) {
      static const GTypeInfo info = {
         sizeof (ViewOvBoxClass),
         NULL, /* BaseInit */
         NULL, /* BaseFinalize */
         (GClassInitFunc)ViewOvBoxClassInit,
         NULL,
         NULL, /* Class Data */
         sizeof (ViewOvBox),
         0, /* n_preallocs */
         (GInstanceInitFunc)ViewOvBoxInit,
      };

      type = g_type_register_static(GTK_TYPE_BOX, "ViewOvBox", &info, 0);
   }

   return type;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBox_New --
 *
 *      Create a new ViewOvBox GTK+ widget.
 *
 * Results:
 *      The widget
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

GtkWidget *
ViewOvBox_New(void)
{
   ViewOvBox *that;

   that = VIEW_OV_BOX(g_object_new(VIEW_TYPE_OV_BOX, NULL));

   return GTK_WIDGET(that);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBox_SetUnder --
 *
 *      Set the under widget of a ViewOvBox.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
ViewOvBox_SetUnder(ViewOvBox *that,   // IN
                   GtkWidget *widget) // IN
{
   g_return_if_fail(that != NULL);

   ViewOvBoxSetChild(that, &that->priv->under, that->priv->underWin, widget);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBox_SetOver --
 *
 *      Set the over widget of a ViewOvBox.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
ViewOvBox_SetOver(ViewOvBox *that,   // IN
                  GtkWidget *widget) // IN
{
   g_return_if_fail(that != NULL);

   VIEW_OV_BOX_GET_CLASS(that)->set_over(that, widget);
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBox_SetMin --
 *
 *      Set the 'min' property of a ViewOvBox, i.e. the number of pixel of the
 *      'over' child that should always be displayed without overlapping on the
 *      'under' child.
 *
 *      Using a value of -1 displays the 'over' child entirely.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
ViewOvBox_SetMin(ViewOvBox *that,  // IN
                 unsigned int min) // IN
{
   g_return_if_fail(that != NULL);

   that->priv->min = min;
   gtk_widget_queue_resize(GTK_WIDGET(that));
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBox_SetFraction --
 *
 *      Set the 'fraction' property of a ViewOvBox, i.e. how much of the 'over'
 *      child should overlap on the 'under' child.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
ViewOvBox_SetFraction(ViewOvBox *that, // IN
                      double fraction) // IN
{
   g_return_if_fail(that != NULL);
   g_return_if_fail(fraction >=0 && fraction <= 1);

   that->priv->fraction = fraction;
   if (GTK_WIDGET_REALIZED(that)) {
      int x;
      int y;
      int width;
      int height;

      ViewOvBoxGetOverGeometry(that, &x, &y, &width, &height);
      gdk_window_move(that->priv->overWin, x, y);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBox_GetFraction --
 *
 *      Retrieve the 'fraction' property of a ViewOvBox.
 *
 * Results:
 *      The value
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

double
ViewOvBox_GetFraction(ViewOvBox *that)
{
   g_return_val_if_fail(that != NULL, 0);

   return that->priv->fraction;
}


/*
 *-----------------------------------------------------------------------------
 *
 * ViewOvBox_SetVerticalOffset --
 *
 *      Set the 'vertical offset' property of a ViewOvBox. Normally, we base
 *      our position on the top edge of the window. This allows us to
 *      be placed somewhere in the middle of a widget.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
ViewOvBox_SetVerticalOffset(ViewOvBox *that, // IN
                            gint offset)     // IN
{
   g_return_if_fail(that != NULL);

   that->priv->verticalOffset = offset;
   if (GTK_WIDGET_REALIZED(that)) {
      int x;
      int y;
      int width;
      int height;

      ViewOvBoxGetOverGeometry(that, &x, &y, &width, &height);
      gdk_window_move(that->priv->overWin, x, y);
   }
}
