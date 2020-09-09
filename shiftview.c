// "arg->i" stores the number of tags to shift right (positive value)
//          or left (negative value)
void
shiftview(const Arg *arg)
{
	Arg a;
	Client *c;
	unsigned visible = 0;
	int i = arg->i;
	int count = 0;
	int nextseltags, curseltags = selmon->tagset[selmon->seltags];

	do {
		if (i > 0) // left circular shift
			nextseltags = (curseltags << i) | (curseltags >> (LENGTH(tags) - i));
		else // right circular shift
			nextseltags = curseltags >> (- i) | (curseltags << (LENGTH(tags) + i));

		// Check if tag is visible
		wl_list_for_each(c, &clients, link) {
			if (nextseltags & c->tags) {
				visible = 1;
				break;
			}
		}

		i += arg->i;
	} while (!visible && ++count <= LENGTH(tags));

	if (count <= LENGTH(tags)) {
		a.i = nextseltags;
		view(&a);
	}
}
