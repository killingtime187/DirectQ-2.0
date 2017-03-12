// http://www.altdevblogaday.com/2011/07/07/writing-a-pre-main-function-forcing-global-initialization-order-within-vc/
#pragma warning(disable: 4075)
#pragma init_seg (".CRT$XCB")

void Heap_Init (void);

class c_blog_first_class_construction
{
public:
	c_blog_first_class_construction (void)
	{
		// call anything that needs to be setup immediately on entry but before other pre-main constructors run from here
		Heap_Init ();
	}

	~c_blog_first_class_construction (void)
	{
	}
};

static c_blog_first_class_construction blog_first_class_construction;

