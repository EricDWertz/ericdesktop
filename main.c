#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gtk/gtkgl.h>
#include <gio/gio.h>

#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glu.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GtkWidget* window;
GtkWidget* drawing_area;

gboolean isConfigured = FALSE;

float transition_alpha = 0.0f;
//Texture coordinates for background
float tx1 = 0.0f, ty1 = 0.0f, tw1 = 1.0f, th1 = 1.0f;
float tx2 = 0.0f, ty2 = 0.0f, tw2 = 1.0f, th2 = 1.0f;

int screen_width;
int screen_height;

GSettings* gsettings;

GLuint shaderProgram;
GLuint tex1Map;
GLuint tex2Map;
GLuint transitionAlpha;

const GLchar* vertSource = "varying vec4 texCoord;"
    "void main(void) {"
    "	gl_Position = gl_Vertex;"
    "	texCoord = gl_MultiTexCoord0;"
    "}";

const GLchar* fragSource = "varying vec4 texCoord;"
    "uniform sampler2D tex1Map;"
    "uniform sampler2D tex2Map;"
    "uniform float transitionAlpha;"
    "void main (void) {"
    "   vec4 tex1 = texture2D( tex1Map, texCoord.xy );"
    "   vec4 tex2 = texture2D( tex2Map, texCoord.zw );"
    "	gl_FragColor = mix( tex2, tex1, transitionAlpha );"
    "}";

void render_gl();

void load_wallpaper_shaders()
{
    GLuint vertShader, fragShader;
    char buffer[2048];

    vertShader = glCreateShader( GL_VERTEX_SHADER );
    fragShader = glCreateShader( GL_FRAGMENT_SHADER );
    printf( "Created Shaders\n" );

    glShaderSource( vertShader, 1, &vertSource, NULL );
    glShaderSource( fragShader, 1, &fragSource, NULL );

    glCompileShader( vertShader );
    glCompileShader( fragShader );

    glGetShaderInfoLog( vertShader, 2048, NULL, buffer );
    printf( buffer );
    glGetShaderInfoLog( fragShader, 2048, NULL, buffer );
    printf( buffer );

    shaderProgram = glCreateProgram();

    glAttachShader( shaderProgram, vertShader );
    glAttachShader( shaderProgram, fragShader );

    glLinkProgram( shaderProgram );
    glGetProgramInfoLog( shaderProgram, 2048, NULL, buffer );
    printf( buffer );

    tex1Map = glGetUniformLocation( shaderProgram, "tex1Map" );
    tex2Map = glGetUniformLocation( shaderProgram, "tex2Map" );
    transitionAlpha = glGetUniformLocation( shaderProgram, "transitionAlpha" );
}

void load_wallpaper_pixels(GdkPixbuf* pixbuf)
{
	int width=gdk_pixbuf_get_width(pixbuf);
	int height=gdk_pixbuf_get_height(pixbuf);
	int rowstride=gdk_pixbuf_get_rowstride(pixbuf);
	int nchannels=gdk_pixbuf_get_n_channels(pixbuf);
	
	//ASSERT N CHANNELS IS 4 HERE

	printf("w: %i h: %i\n  stride: %i channels: %i\n",
		width,
		height,
		rowstride,
		nchannels);

	guchar* pixels=gdk_pixbuf_get_pixels(pixbuf);

	int pixelcount=width*height;
	int i, x;
	int p=0;
	for(i=0;i<height;i++)
	{	
		if(nchannels==3)
		{
			memcpy(pixels+i*width*3, pixels+i*rowstride, width*3);
		}
		p+=rowstride;
	}

	int ar, ag, ab;
	ar = 0; ag = 0; ab = 0;
	for( i = 0; i < pixelcount*3; i+=3 )
	{
		ar += pixels[i]; 
		ag += pixels[i + 1]; 
		ab += pixels[i + 2];
	}

	printf( "Average color test: %i, %i, %i\n", ar / pixelcount, ag / pixelcount, ab / pixelcount ); 
	char buffer[64];
	sprintf( buffer, "#%02X%02X%02X", ar/pixelcount, ag/pixelcount, ab/pixelcount );

	//GConfClient* client = gconf_client_get_default();
	//gconf_client_set_string( client, "/desktop/eric/theme_color", buffer, NULL );
    g_settings_set_string( gsettings, "primary-color", buffer );
	
    //Calculate texture coordinates
    tx2 = tx1; ty2 = ty1; tw2 = tw1; th2 = th1; //Copy old values
    float aspect = (float)screen_width/(float)screen_height;
    float sw, sh;
    float targetheight = screen_height;
    float targetwidth = screen_width;
    float mw = targetwidth/(float)width;
    float mh = targetheight/(float)height;

    if( mh > mw )
    {
       targetwidth = targetheight / (float)height * (float)width;
    }
    else if ( mw > mh )
    {
        targetheight = targetwidth / (float)width * (float)height;
    }

    if( targetwidth == screen_width )
    {
        //Scale width, crop height
        tx1 = 0.0f;
        tw1 = 1.0f;
        th1 = ((float)width / aspect)/(float)height;
        ty1 = (1.0f - th1) / 2.0f;
    }
    else
    {
        //Scale height, crop width
        ty1 = 0.0f;
        th1 = 1.0f;
        tw1 = ((float)height * aspect)/(float)width;
        tx1 = (1.0f - tw1) / 2.0f;
    }

    printf( "target width: %f, target height: %f\n", targetwidth, targetheight );
    printf( "tx: %f, ty: %f, tw: %f, th: %f\n", tx1, ty1, tw1, th1 );

	glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,pixels);
}

gboolean animation_timer( gpointer user )
{
	render_gl();

	return ( transition_alpha < 1.0 );
}

GLuint texture=0;
GLuint texture2=0;
void load_background_texture(const char* path)
{
	//Get raw data from gdkPixbuf!
	GError* error=NULL;
	GdkPixbuf* pixbuf=gdk_pixbuf_new_from_file(path,&error);
	if(!pixbuf) 
	{
		printf("Error loading background %s\n%s\n",error->message);
		g_error_free(error);
		return;
	}
	
	if(texture2!=0) glDeleteTextures(1,&texture2);
	texture2=texture;
	
	glGenTextures(1,&texture);
	
	glBindTexture(GL_TEXTURE_2D,texture);
	
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	
	load_wallpaper_pixels(pixbuf);

	//free GdkPixbuf
	g_object_unref(pixbuf);

	transition_alpha = 0.0;
	g_timeout_add( 16, animation_timer, NULL );
} 

void configure_event( GtkWidget* widget, GdkEventConfigure *event, gpointer user )
{
 	gtk_window_set_type_hint( GTK_WINDOW( window ), GDK_WINDOW_TYPE_HINT_DESKTOP );
	
	GdkGLContext* gl_context = gtk_widget_get_gl_context( widget );
	GdkGLDrawable *gl_drawable = gtk_widget_get_gl_drawable( widget );

	if( !gdk_gl_drawable_gl_begin( gl_drawable, gl_context ) )
		g_assert_not_reached();

    GLenum err = glewInit();
    if( GLEW_OK != err )
        fprintf( stderr, "Error: %s\n", glewGetErrorString( err ) );
    else
        printf("glew ok!\n");

    load_wallpaper_shaders();


	if( !isConfigured )
	{
		isConfigured = TRUE;
	}

	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	//GConfClient* client = gconf_client_get_default();
	load_background_texture( g_settings_get_string( gsettings, "picture-uri" ) );

	gdk_gl_drawable_gl_end( gl_drawable );
};

void render_gl()
{
	GdkGLContext* gl_context = gtk_widget_get_gl_context( drawing_area );
	GdkGLDrawable *gl_drawable = gtk_widget_get_gl_drawable( drawing_area );

	if( !gdk_gl_drawable_gl_begin( gl_drawable, gl_context ) )
		g_assert_not_reached();

	glClearColor( 0.0, 0.0, 0.0, 1.0 );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glUseProgram( shaderProgram );
	glEnable(GL_TEXTURE_2D);

    //Bind Textures to shader
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, texture );
    glUniform1i( tex1Map, 0 );
    glActiveTexture( GL_TEXTURE1 );
    glBindTexture( GL_TEXTURE_2D, texture2 );
    glUniform1i( tex2Map, 1 );

    glUniform1f( transitionAlpha, transition_alpha );

    
    glLoadIdentity();                                  
    glBegin(GL_QUADS);
    glColor3f(1.0,1.0,1.0);
    
    glTexCoord4f(tx1,ty1,tx2,ty2);
    glVertex2f(-1,1);
    
    glTexCoord4f(tx1+tw1,ty1,tx2+tw2,ty2);
    glVertex2f(1,1);
    
    glTexCoord4f(tx1+tw1,ty1+th1,tx2+tw2,ty2+th2);
    glVertex2f(1,-1);
    
    glTexCoord4f(tx1,ty1+th1,tx2,ty2+th2);
    glVertex2f(-1,-1);        
    glEnd();                 

    transition_alpha += 0.025f;  

	if( gdk_gl_drawable_is_double_buffered( gl_drawable ) )
		gdk_gl_drawable_swap_buffers( gl_drawable );
	else
		glFlush();
	
	gdk_gl_drawable_gl_end( gl_drawable );
}

void expose_event( GtkWidget* widget, GdkEventExpose *event, gpointer user )
{
	render_gl();
}	

void init_gl( int argc, char* argv[] )
{
	gtk_gl_init( &argc, &argv );
	
	GdkGLConfig *gl_config = gdk_gl_config_new_by_mode( (GdkGLConfigMode)(GDK_GL_MODE_RGBA,
								GDK_GL_MODE_DEPTH |
								GDK_GL_MODE_DOUBLE) );

	if( !gl_config )
		g_assert_not_reached();
	
	if( !gtk_widget_set_gl_capability( drawing_area, gl_config, NULL, TRUE, GDK_GL_RGBA_TYPE) )
		g_assert_not_reached();


	g_signal_connect( drawing_area, "expose-event", G_CALLBACK( expose_event ), NULL );
	g_signal_connect( drawing_area, "configure-event", G_CALLBACK( configure_event ), NULL );
};

//void gconf_wallpaper_changed( GConfClient* client, guint cnxn_id, GConfEntry *entry, gpointer user_data )
//{
//	printf( "Got change event %s\n", gconf_entry_get_key( entry ) );
//	if( strcmp( gconf_entry_get_key( entry ), "/desktop/eric/wallpaper_path" ) == 0 )
//		load_background_texture( gconf_value_get_string( gconf_entry_get_value( entry ) ) );	
//} 

void gsettings_value_changed( GSettings *settings, const gchar *key, gpointer user )
{
    if( strcmp( key, "picture-uri" ) == 0 )
    {
        load_background_texture( g_settings_get_string( settings, "picture-uri" ) );
    }
}

gboolean button_press_event( GtkWidget* widget, GdkEventButton* event, gpointer user )
{
	if( event->button == 3 )
	{
		char cmd[128];
		sprintf( cmd, "ericlaunch -w -p %i %i -d 480 360 -s 64", (int)event->x, (int)event->y );
		system( cmd );
	}
	//Double click to show desktop hack
	if( event->type == GDK_2BUTTON_PRESS )
		system( "xdotool key alt+d" );
}

gboolean screen_changed_event( GtkWidget* widget, GdkScreen *old_screen, gpointer user )
{
	GdkScreen* screen = gdk_screen_get_default();
	screen_width = gdk_screen_get_width( screen );
	screen_height = gdk_screen_get_height( screen );

	//gtk_window_move( GTK_WINDOW( window ), 0, 0 );
	gtk_window_resize( GTK_WINDOW( window ), screen_width, screen_height );
 	gtk_window_set_type_hint( GTK_WINDOW( window ), GDK_WINDOW_TYPE_HINT_DESKTOP );
}

int main( int argc, char* argv[] )
{
	gtk_init( &argc, &argv );

	GdkScreen* screen = gdk_screen_get_default();
	screen_width = gdk_screen_get_width( screen );
	screen_height = gdk_screen_get_height( screen );
	
	window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
 	gtk_window_set_type_hint( GTK_WINDOW( window ), GDK_WINDOW_TYPE_HINT_DESKTOP );
	gtk_window_resize( GTK_WINDOW( window ), screen_width, screen_height );
	//gtk_window_move( GTK_WINDOW( window ), 0, 0 );
	gtk_window_fullscreen( GTK_WINDOW( window ) ); 
	gtk_widget_add_events( window, GDK_BUTTON_PRESS_MASK );
	g_signal_connect( G_OBJECT(window), "button-press-event", G_CALLBACK(button_press_event), NULL );
	g_signal_connect( G_OBJECT(window), "screen-changed", G_CALLBACK(screen_changed_event), NULL );
	

	drawing_area = gtk_drawing_area_new();
	
	gtk_container_add( GTK_CONTAINER( window ), drawing_area );

	init_gl( argc, argv );

    GSettingsSchema* gsettings_schema;

    gsettings_schema = g_settings_schema_source_lookup( g_settings_schema_source_get_default(),
                    "org.gnome.desktop.background",
                    TRUE );
    if( gsettings_schema )
    {
        g_settings_schema_unref (gsettings_schema);
        gsettings_schema = NULL;
        gsettings = g_settings_new ( "org.gnome.desktop.background" );
    }

    g_signal_connect_data( gsettings, "changed", G_CALLBACK( gsettings_value_changed ), NULL, 0, 0 );

	//GConfClient* client = gconf_client_get_default();
	//gconf_client_add_dir( client, "/desktop/eric", GCONF_CLIENT_PRELOAD_ONELEVEL, NULL );
	//gconf_client_notify_add( client, "/desktop/eric/wallpaper_path", gconf_wallpaper_changed, NULL, NULL, NULL );

	gtk_widget_show_all( window );

	gtk_main();
	
	return 0;
}	
	
