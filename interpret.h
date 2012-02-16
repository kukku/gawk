/*
 * interpret ---  code is a list of instructions to run.
 */

#ifdef EXE_MPFR
#define NV(r)	r->mpfr_numbr
#else
#define NV(r)	r->numbr
#endif


int
r_interpret(INSTRUCTION *code)
{
	INSTRUCTION *pc;   /* current instruction */
	OPCODE op;	/* current opcode */
	NODE *r = NULL;
	NODE *m;
	INSTRUCTION *ni;
	NODE *t1, *t2;
	NODE **lhs;
	AWKNUM x;
	int di;
	Regexp *rp;

/* array subscript */
#define mk_sub(n)  	(n == 1 ? POP_SCALAR() : concat_exp(n, TRUE))

#ifdef DEBUGGING
#define JUMPTO(x)	do { post_execute(pc); pc = (x); goto top; } while (FALSE)
#else
#define JUMPTO(x)	do { pc = (x); goto top; } while (FALSE)
#endif

	pc = code;

	/* N.B.: always use JUMPTO for next instruction, otherwise bad things
	 * may happen. DO NOT add a real loop (for/while) below to
	 * replace ' forever {'; this catches failure to use JUMPTO to execute
	 * next instruction (e.g. continue statement).
	 */

	/* loop until hit Op_stop instruction */

	/* forever {  */
top:
		if (pc->source_line > 0)
			sourceline = pc->source_line;

#ifdef DEBUGGING
		if (! pre_execute(& pc))
			goto top;
#endif

		switch ((op = pc->opcode)) {
		case Op_rule:
			currule = pc->in_rule;   /* for sole use in Op_K_next, Op_K_nextfile, Op_K_getline */
			/* fall through */
		case Op_func:
			source = pc->source_file;
			break;

		case Op_atexit:
		{
			int stdio_problem = FALSE;

			/* avoid false source indications */
			source = NULL;
			sourceline = 0;
			(void) nextfile(& curfile, TRUE);	/* close input data file */ 
			/*
			 * This used to be:
			 *
			 * if (close_io() != 0 && ! exiting && exit_val == 0)
			 *      exit_val = 1;
			 *
			 * Other awks don't care about problems closing open files
			 * and pipes, in that it doesn't affect their exit status.
			 * So we no longer do either.
			 */
			(void) close_io(& stdio_problem);
			/*
			 * However, we do want to exit non-zero if there was a problem
			 * with stdout/stderr, so we reinstate a slightly different
			 * version of the above:
			 */
			if (stdio_problem && ! exiting && exit_val == 0)
				exit_val = 1;
		}
			break;

		case Op_stop:
			return 0;

		case Op_push_i:
			m = pc->memory;
			if (! do_traditional && (m->flags & INTLSTR) != 0) {
				char *orig, *trans, save;

				save = m->stptr[m->stlen];
				m->stptr[m->stlen] = '\0';
				orig = m->stptr;
				trans = dgettext(TEXTDOMAIN, orig);
				m->stptr[m->stlen] = save;
				m = make_string(trans, strlen(trans));
			} else
				UPREF(m);
			PUSH(m);
			break;

		case Op_push:
		case Op_push_arg:
		{
			NODE *save_symbol;
			int isparam = FALSE;

			save_symbol = m = pc->memory;
			if (m->type == Node_param_list) {
				isparam = TRUE;
				save_symbol = m = GET_PARAM(m->param_cnt);
				if (m->type == Node_array_ref)
					m = m->orig_array;
			}
				
			switch (m->type) {
			case Node_var:
				if (do_lint && var_uninitialized(m))
					lintwarn(isparam ?
						_("reference to uninitialized argument `%s'") :
						_("reference to uninitialized variable `%s'"),
								save_symbol->vname);
				m = m->var_value;
				UPREF(m);
				PUSH(m);
				break;

			case Node_var_new:
				m->type = Node_var;
				m->var_value = dupnode(Nnull_string);
				if (do_lint)
					lintwarn(isparam ?
						_("reference to uninitialized argument `%s'") :
						_("reference to uninitialized variable `%s'"),
								save_symbol->vname);
				m = dupnode(Nnull_string);
				PUSH(m);
				break;

			case Node_var_array:
				if (op == Op_push_arg)
					PUSH(m);
				else
					fatal(_("attempt to use array `%s' in a scalar context"),
							array_vname(save_symbol));
				break;

			default:
				cant_happen();
			}
		}
			break;	

		case Op_push_param:		/* function argument */
			m = pc->memory;
			if (m->type == Node_param_list)
				m = GET_PARAM(m->param_cnt);
			if (m->type == Node_var) {
				m = m->var_value;
				UPREF(m);
				PUSH(m);
		 		break;
			}
 			/* else
				fall through */
		case Op_push_array:
			PUSH(pc->memory);
			break;

		case Op_push_lhs:
			lhs = get_lhs(pc->memory, pc->do_reference);
			PUSH_ADDRESS(lhs);
			break;

		case Op_subscript:
			t2 = mk_sub(pc->sub_count);
			t1 = POP_ARRAY();

			if (do_lint && in_array(t1, t2) == NULL) {
				t2 = force_string(t2);
				lintwarn(_("reference to uninitialized element `%s[\"%.*s\"]'"),
					array_vname(t1), (int) t2->stlen, t2->stptr);
				if (t2->stlen == 0)
					lintwarn(_("subscript of array `%s' is null string"), array_vname(t1));
			}

			r = *assoc_lookup(t1, t2);
			DEREF(t2);
			if (r->type == Node_val)
				UPREF(r);
			PUSH(r);
			break;

		case Op_sub_array:
			t2 = mk_sub(pc->sub_count);
			t1 = POP_ARRAY();
			r = in_array(t1, t2);
			if (r == NULL) {
				r = make_array();
				r->parent_array = t1;
				*assoc_lookup(t1, t2) = r;
				t2 = force_string(t2);
				r->vname = estrdup(t2->stptr, t2->stlen);	/* the subscript in parent array */
			} else if (r->type != Node_var_array) {
				t2 = force_string(t2);
				fatal(_("attempt to use scalar `%s[\"%.*s\"]' as an array"),
						array_vname(t1), (int) t2->stlen, t2->stptr);
			}

			DEREF(t2);
			PUSH(r);
			break;

		case Op_subscript_lhs:
			t2 = mk_sub(pc->sub_count);
			t1 = POP_ARRAY();
			if (do_lint && in_array(t1, t2) == NULL) {
				t2 = force_string(t2);
				if (pc->do_reference) 
					lintwarn(_("reference to uninitialized element `%s[\"%.*s\"]'"),
						array_vname(t1), (int) t2->stlen, t2->stptr);
				if (t2->stlen == 0)
					lintwarn(_("subscript of array `%s' is null string"), array_vname(t1));
			}

			lhs = assoc_lookup(t1, t2);
			if ((*lhs)->type == Node_var_array) {
				t2 = force_string(t2);
				fatal(_("attempt to use array `%s[\"%.*s\"]' in a scalar context"),
						array_vname(t1), (int) t2->stlen, t2->stptr);
			}

			DEREF(t2);
			PUSH_ADDRESS(lhs);
			break;

		case Op_field_spec:
			t1 = TOP_SCALAR();
			lhs = r_get_field(t1, (Func_ptr *) 0, TRUE);
			decr_sp();
			DEREF(t1);
			r = dupnode(*lhs);     /* can't use UPREF here */
			PUSH(r);
			break;

		case Op_field_spec_lhs:
			t1 = TOP_SCALAR();
			lhs = r_get_field(t1, &pc->target_assign->field_assign, pc->do_reference);
			decr_sp();
			DEREF(t1);
			PUSH_ADDRESS(lhs);
			break;

		case Op_lint:
			if (do_lint) {
				switch (pc->lint_type) {
				case LINT_assign_in_cond:
					lintwarn(_("assignment used in conditional context"));
					break;

				case LINT_no_effect:
					lintwarn(_("statement has no effect"));
					break;

				default:
					cant_happen();
				}
			}
			break;

		case Op_K_break:
		case Op_K_continue:
		case Op_jmp:
			JUMPTO(pc->target_jmp);

		case Op_jmp_false:
			r = POP_SCALAR();
			di = eval_condition(r);
			DEREF(r);
			if (! di)
				JUMPTO(pc->target_jmp);
			break;

		case Op_jmp_true:
			r = POP_SCALAR();
			di = eval_condition(r);
			DEREF(r);			
			if (di)
				JUMPTO(pc->target_jmp);
			break;

		case Op_and:
		case Op_or:
			t1 = POP_SCALAR();
			di = eval_condition(t1);
			DEREF(t1);
			if ((op == Op_and && di) || (op == Op_or && ! di))
				break;
			r = node_Boolean[di];
			UPREF(r);
			PUSH(r);
			ni = pc->target_jmp;
			JUMPTO(ni->nexti);

		case Op_and_final:
		case Op_or_final:
			t1 = TOP_SCALAR();
			r = node_Boolean[eval_condition(t1)];
			DEREF(t1);
			UPREF(r);
			REPLACE(r);
			break;

		case Op_not:
			t1 = TOP_SCALAR();
			r = node_Boolean[! eval_condition(t1)];
			DEREF(t1);
			UPREF(r);
			REPLACE(r);
			break;

		case Op_equal:
			r = node_Boolean[cmp_scalar() == 0];
			UPREF(r);
			REPLACE(r);
			break;

		case Op_notequal:
			r = node_Boolean[cmp_scalar() != 0];
			UPREF(r);
			REPLACE(r);
			break;

		case Op_less:
			r = node_Boolean[cmp_scalar() < 0];
			UPREF(r);
			REPLACE(r);
			break;

		case Op_greater:
			r = node_Boolean[cmp_scalar() > 0];
			UPREF(r);
			REPLACE(r);
			break;

		case Op_leq:
			r = node_Boolean[cmp_scalar() <= 0];
			UPREF(r);
			REPLACE(r);
			break;

		case Op_geq:
			r = node_Boolean[cmp_scalar() >= 0];
			UPREF(r);
			REPLACE(r);
			break;

		case Op_plus_i:
			t2 = force_number(pc->memory);
			goto plus;
		case Op_plus:
			t2 = POP_NUMBER();
plus:
			t1 = TOP_NUMBER();
#ifdef EXE_MPFR
			r = mpfr_node();
			mpfr_add(NV(r), NV(t1), NV(t2), RND_MODE);
#else
			r = make_number(NV(t1) + NV(t2));
#endif
			DEREF(t1);
			if (op == Op_plus)
				DEREF(t2);
			REPLACE(r);
			break;

		case Op_minus_i:
			t2 = force_number(pc->memory);
			goto minus;
		case Op_minus:
			t2 = POP_NUMBER();
minus:
			t1 = TOP_NUMBER();
#ifdef EXE_MPFR
			r = mpfr_node();
			mpfr_sub(NV(r), NV(t1), NV(t2), RND_MODE);
#else
			r = make_number(NV(t1) - NV(t2));
#endif
			DEREF(t1);
			if (op == Op_minus)
				DEREF(t2);
			REPLACE(r);
			break;

		case Op_times_i:
			t2 = force_number(pc->memory);
			goto times;
		case Op_times:
			t2 = POP_NUMBER();
times:
			t1 = TOP_NUMBER();
#ifdef EXE_MPFR
			r = mpfr_node();
			mpfr_mul(NV(r), NV(t1), NV(t2), RND_MODE);
#else
			r = make_number(NV(t1) * NV(t2));
#endif
			DEREF(t1);
			if (op == Op_times)
				DEREF(t2);
			REPLACE(r);
			break;

		case Op_exp_i:
			t2 = force_number(pc->memory);
			goto exp;
		case Op_exp:
			t2 = POP_NUMBER();
exp:
			t1 = TOP_NUMBER();
#ifdef EXE_MPFR
			r = mpfr_node();
			mpfr_pow(NV(r), NV(t1), NV(t2), RND_MODE);
#else
			x = calc_exp(NV(t1), NV(t2));
			r = make_number(x);
#endif
			DEREF(t1);
			if (op == Op_exp)
				DEREF(t2);
			REPLACE(r);
			break;

		case Op_quotient_i:
			t2 = force_number(pc->memory);
			goto quotient;
		case Op_quotient:
			t2 = POP_NUMBER();
quotient:
			t1 = TOP_NUMBER();
#ifdef EXE_MPFR
			r = mpfr_node();
			mpfr_div(NV(r), NV(t1), NV(t2), RND_MODE);
#else
			if (NV(t2) == 0)
				fatal(_("division by zero attempted"));
			x = NV(t1) / NV(t2);
			r = make_number(x);
#endif
			DEREF(t1);
			if (op == Op_quotient)
				DEREF(t2);
			REPLACE(r);
			break;		

		case Op_mod_i:
			t2 = force_number(pc->memory);
			goto mod;
		case Op_mod:
			t2 = POP_NUMBER();
mod:
			t1 = TOP_NUMBER();
#ifdef EXE_MPFR
			r = mpfr_node();
			mpfr_fmod(NV(r), NV(t1), NV(t2), RND_MODE);
#else
			if (NV(t2) == 0)
				fatal(_("division by zero attempted in `%%'"));
#ifdef HAVE_FMOD
			x = fmod(NV(t1), NV(t2));
#else	/* ! HAVE_FMOD */
			(void) modf(NV(t1) / NV(t2), &x);
			x = NV(t1) - x * NV(t2);
#endif	/* ! HAVE_FMOD */
			r = make_number(x);
#endif
			DEREF(t1);
			if (op == Op_mod)
				DEREF(t2);
			REPLACE(r);
			break;

		case Op_preincrement:
		case Op_predecrement:
			x = op == Op_preincrement ? 1.0 : -1.0;
			lhs = TOP_ADDRESS();
			t1 = *lhs;
			force_number(t1);
			if (t1->valref == 1 && t1->flags == (MALLOC|NUMCUR|NUMBER)) {
				/* optimization */
#ifdef EXE_MPFR
				mpfr_add_d(NV(t1), NV(t1), x, RND_MODE);
#else
				NV(t1) += x;
#endif
				r = t1;
			} else {
#ifdef EXE_MPFR
				r = *lhs = mpfr_node();
				mpfr_add_d(NV(r), NV(t1), x, RND_MODE);
#else
				r = *lhs = make_number(NV(t1) + x);
#endif
				unref(t1);
			}
			UPREF(r);
			REPLACE(r);
			break;

		case Op_postincrement:
		case Op_postdecrement:
			x = op == Op_postincrement ? 1.0 : -1.0;
			lhs = TOP_ADDRESS();
			t1 = *lhs;
			force_number(t1);
#ifdef EXE_MPFR
			r = mpfr_node();
			mpfr_set(NV(r), NV(t1), RND_MODE);	/* r = t1 */
			if (t1->valref == 1 && t1->flags == (MALLOC|NUMCUR|NUMBER)) {
				/* optimization */
				mpfr_add_d(NV(t1), NV(t1), x, RND_MODE);
			} else {
				t2 = *lhs = mpfr_node();
				mpfr_add_d(NV(t2), NV(t1), x, RND_MODE);
				unref(t1);
			}
#else
			r = make_number(NV(t1));
			if (t1->valref == 1 && t1->flags == (MALLOC|NUMCUR|NUMBER)) {
 				/* optimization */
				NV(t1) += x;
			} else {
				*lhs = make_number(NV(t1) + x);
				unref(t1);
			}
#endif
			REPLACE(r);
			break;

		case Op_unary_minus:
			t1 = TOP_NUMBER();
#ifdef EXE_MPFR
			r = mpfr_node();
			mpfr_set(NV(r), NV(t1), RND_MODE);	/* r = t1 */
			mpfr_neg(NV(r), NV(r), RND_MODE);	/* change sign */
#else
			r = make_number(-NV(t1));
#endif
			DEREF(t1);
			REPLACE(r);
			break;

		case Op_store_sub:
			/* array[sub] assignment optimization,
			 * see awkgram.y (optimize_assignment)
			 */
			t1 = get_array(pc->memory, TRUE);	/* array */
			t2 = mk_sub(pc->expr_count);	/* subscript */
 			lhs = assoc_lookup(t1, t2);
			if ((*lhs)->type == Node_var_array) {
				t2 = force_string(t2);
				fatal(_("attempt to use array `%s[\"%.*s\"]' in a scalar context"),
						array_vname(t1), (int) t2->stlen, t2->stptr);
			}
			DEREF(t2);
			unref(*lhs);
			*lhs = POP_SCALAR();
			break;

		case Op_store_var:
			/* simple variable assignment optimization,
			 * see awkgram.y (optimize_assignment)
			 */
	
			lhs = get_lhs(pc->memory, FALSE);
			unref(*lhs);
			r = pc->initval;	/* constant initializer */
			if (r == NULL)
				*lhs = POP_SCALAR();
			else {
				UPREF(r);
				*lhs = r;
			}
			break;

		case Op_store_field:
		{
			/* field assignment optimization,
			 * see awkgram.y (optimize_assignment)
			 */

			Func_ptr assign;
			t1 = TOP_SCALAR();
			lhs = r_get_field(t1, & assign, FALSE);
			decr_sp();
			DEREF(t1);
			unref(*lhs);
			*lhs = POP_SCALAR();
			assert(assign != NULL);
			assign();
		}
			break;

		case Op_assign_concat:
			/* x = x ... string concatenation optimization */
			lhs = get_lhs(pc->memory, FALSE);
			t1 = force_string(*lhs);
			t2 = POP_STRING();

			free_wstr(*lhs);

			if (t1 != *lhs) {
				unref(*lhs);
				*lhs = dupnode(t1);
			}

			if (t1 != t2 && t1->valref == 1 && (t1->flags & MPFN) == 0) {
				size_t nlen = t1->stlen + t2->stlen;

				erealloc(t1->stptr, char *, nlen + 2, "r_interpret");
				memcpy(t1->stptr + t1->stlen, t2->stptr, t2->stlen);
				t1->stlen = nlen;
				t1->stptr[nlen] = '\0';
				t1->flags &= ~(NUMCUR|NUMBER|NUMINT);
			} else {
				size_t nlen = t1->stlen + t2->stlen;  
				char *p;

				emalloc(p, char *, nlen + 2, "r_interpret");
				memcpy(p, t1->stptr, t1->stlen);
				memcpy(p + t1->stlen, t2->stptr, t2->stlen);
				unref(*lhs);
				t1 = *lhs = make_str_node(p, nlen, ALREADY_MALLOCED); 
			}
			DEREF(t2);
			break;

		case Op_assign:
			lhs = POP_ADDRESS();
			r = TOP_SCALAR();
			unref(*lhs);
			*lhs = r;
			UPREF(r);
			REPLACE(r);
			break;

		/* numeric assignments */
		case Op_assign_plus:
		case Op_assign_minus:
		case Op_assign_times:
		case Op_assign_quotient:
		case Op_assign_mod:
		case Op_assign_exp:
#ifdef EXE_MPFR
			op_mpfr_assign(op);
#else
			op_assign(op);
#endif
			break;

		case Op_var_update:        /* update value of NR, FNR or NF */
			pc->update_var();
			break;

		case Op_var_assign:
		case Op_field_assign:
		        r = TOP();
#ifdef EXE_MPFR
               		di = mpfr_sgn(NV(r));
#else
			if (NV(r) < 0.0)
				di = -1;
			else
				di = (NV(r) > 0.0);
#endif

			if (pc->assign_ctxt == Op_sub_builtin
				&& di == 0	/* top of stack has a number == 0 */
			) {
				/* There wasn't any substitutions. If the target is a FIELD,
				 * this means no field re-splitting or $0 reconstruction.
				 * Skip the set_FOO routine if the target is a special variable.
				 */

				break;
			} else if ((pc->assign_ctxt == Op_K_getline
					|| pc->assign_ctxt == Op_K_getline_redir)
				&& di <= 0 	/* top of stack has a number <= 0 */
			) {
				/* getline returned EOF or error */

				break;
			}

			if (op == Op_var_assign)
				pc->assign_var();
			else
				pc->field_assign();
			break;

		case Op_concat:
			r = concat_exp(pc->expr_count, pc->concat_flag & CSUBSEP);
			PUSH(r);
			break;

		case Op_K_case:
			if ((pc + 1)->match_exp) {
				/* match a constant regex against switch expression instead of $0. */

				m = POP();	/* regex */
				t2 = TOP_SCALAR();	/* switch expression */
				t2 = force_string(t2);
				rp = re_update(m);
				di = (research(rp, t2->stptr, 0, t2->stlen,
							avoid_dfa(m, t2->stptr, t2->stlen)) >= 0);
			} else {
				t1 = POP_SCALAR();	/* case value */
				t2 = TOP_SCALAR();	/* switch expression */
				di = (cmp_nodes(t2, t1) == 0);
				DEREF(t1);
			}

			if (di) {
				/* match found */
				t2 = POP_SCALAR();
				DEREF(t2);
				JUMPTO(pc->target_jmp);
			}
			break;

		case Op_K_delete:
			t1 = POP_ARRAY();
			do_delete(t1, pc->expr_count);
			stack_adj(-pc->expr_count);
			break;

		case Op_K_delete_loop:
			t1 = POP_ARRAY();
			lhs = POP_ADDRESS();	/* item */
			do_delete_loop(t1, lhs);
			break;

		case Op_in_array:
			t1 = POP_ARRAY();
			t2 = mk_sub(pc->expr_count);
			r = node_Boolean[(in_array(t1, t2) != NULL)];
			DEREF(t2);
			UPREF(r);
			PUSH(r);
			break;

		case Op_arrayfor_init:
		{
			NODE **list = NULL;
			NODE *array, *sort_str;
			size_t num_elems = 0;
			static NODE *sorted_in = NULL;
			const char *how_to_sort = "@unsorted";

			/* get the array */
			array = POP_ARRAY();

			/* sanity: check if empty */
			if (array_empty(array))
				goto arrayfor;

			num_elems = array->table_size;

			if (sorted_in == NULL)		/* do this once */
				sorted_in = make_string("sorted_in", 9);

			sort_str = NULL;
			/*
			 * If posix, or if there's no PROCINFO[],
			 * there's no ["sorted_in"], so no sorting
			 */
			if (! do_posix && PROCINFO_node != NULL)
				sort_str = in_array(PROCINFO_node, sorted_in);

			if (sort_str != NULL) {
				sort_str = force_string(sort_str);
				if (sort_str->stlen > 0)
					how_to_sort = sort_str->stptr;
			}

			list = assoc_list(array, how_to_sort, SORTED_IN);

arrayfor:
			getnode(r);
			r->type = Node_arrayfor;
			r->for_list = list;
			r->for_list_size = num_elems;		/* # of elements in list */
			r->cur_idx = -1;			/* current index */
			r->for_array = array;		/* array */
			PUSH(r);

			if (num_elems == 0)
				JUMPTO(pc->target_jmp);   /* Op_arrayfor_final */
		}
			break;

		case Op_arrayfor_incr:
			r = TOP();	/* Node_arrayfor */
			if (++r->cur_idx == r->for_list_size) {
				NODE *array;
				array = r->for_array;	/* actual array */
				if (do_lint && array->table_size != r->for_list_size)
					lintwarn(_("for loop: array `%s' changed size from %ld to %ld during loop execution"),
						array_vname(array), (long) r->for_list_size, (long) array->table_size);
				JUMPTO(pc->target_jmp);	/* Op_arrayfor_final */
			}

			t1 = r->for_list[r->cur_idx];
			lhs = get_lhs(pc->array_var, FALSE);
			unref(*lhs);
			*lhs = dupnode(t1);
			break;

		case Op_arrayfor_final:
			r = POP();
			assert(r->type == Node_arrayfor);
			free_arrayfor(r);
			break;

		case Op_builtin:
			r = pc->builtin(pc->expr_count);
			PUSH(r);
			break;

		case Op_ext_builtin:
		{
			int arg_count = pc->expr_count;

			PUSH_CODE(pc);
			r = pc->builtin(arg_count);
			(void) POP_CODE();
			while (arg_count-- > 0) {
				t1 = POP();
				if (t1->type == Node_val)
					DEREF(t1);
			}
			PUSH(r);
		}
			break;

		case Op_sub_builtin:	/* sub, gsub and gensub */
			r = do_sub(pc->expr_count, pc->sub_flags);
			PUSH(r);
			break;

		case Op_K_print:
			do_print(pc->expr_count, pc->redir_type);
			break;

		case Op_K_printf:
			do_printf(pc->expr_count, pc->redir_type);
			break;

		case Op_K_print_rec:
			do_print_rec(pc->expr_count, pc->redir_type);
			break;

		case Op_push_re:
			m = pc->memory;
			if (m->type == Node_dynregex) {
				r = POP_STRING();
				unref(m->re_exp);
				m->re_exp = r;
			}
			PUSH(m);
			break;
			
		case Op_match_rec:
			m = pc->memory;
			t1 = *get_field(0, (Func_ptr *) 0);
match_re:
			rp = re_update(m);
			/*
			 * Any place where research() is called with a last parameter of
			 * zero, we need to use the avoid_dfa test. This appears here and
			 * in the code for Op_K_case.
			 *
			 * A new or improved dfa that distinguishes beginning/end of
			 * string from beginning/end of line will allow us to get rid of
			 * this hack.
			 *
			 * The avoid_dfa() function is in re.c; it is not very smart.
			 */

			di = research(rp, t1->stptr, 0, t1->stlen,
								avoid_dfa(m, t1->stptr, t1->stlen));
			di = (di == -1) ^ (op != Op_nomatch);
			if (op != Op_match_rec) {
				decr_sp();
				DEREF(t1);
			}
			r = node_Boolean[di];
			UPREF(r);
			PUSH(r);
			break;

		case Op_nomatch:
			/* fall through */
		case Op_match:
			m = pc->memory;
			t1 = TOP_STRING();
			if (m->type == Node_dynregex) {
				unref(m->re_exp);
				m->re_exp = t1;
				decr_sp();
				t1 = TOP_STRING();
			}
			goto match_re;
			break;

		case Op_indirect_func_call:
		{
			NODE *f = NULL;
			int arg_count;

			arg_count = (pc + 1)->expr_count;
			t1 = PEEK(arg_count);	/* indirect var */
			assert(t1->type == Node_val);	/* @a[1](p) not allowed in grammar */
			t1 = force_string(t1);
			if (t1->stlen > 0) {
				/* retrieve function definition node */
				f = pc->func_body;
				if (f != NULL && strcmp(f->vname, t1->stptr) == 0) {
					/* indirect var hasn't been reassigned */

					ni = setup_frame(pc);
					JUMPTO(ni);	/* Op_func */
				}
				f = lookup(t1->stptr);
			}

			if (f == NULL || f->type != Node_func)
				fatal(_("function called indirectly through `%s' does not exist"),
						pc->func_name);	
			pc->func_body = f;     /* save for next call */

			ni = setup_frame(pc);
			JUMPTO(ni);	/* Op_func */
		}

		case Op_func_call:
		{
			NODE *f;

			/* retrieve function definition node */
			f = pc->func_body;
			if (f == NULL) {
				f = lookup(pc->func_name);
				if (f == NULL || (f->type != Node_func && f->type != Node_ext_func))
					fatal(_("function `%s' not defined"), pc->func_name);
				pc->func_body = f;     /* save for next call */
			}

			if (f->type == Node_ext_func) {
				INSTRUCTION *bc;
				char *fname = pc->func_name;
				int arg_count = (pc + 1)->expr_count;

				bc = f->code_ptr;
				assert(bc->opcode == Op_symbol);
				pc->opcode = Op_ext_builtin;	/* self modifying code */
				pc->builtin = bc->builtin;
				pc->expr_count = arg_count;		/* actual argument count */
				(pc + 1)->func_name = fname;	/* name of the builtin */
				(pc + 1)->expr_count = bc->expr_count;	/* defined max # of arguments */
				ni = pc; 
				JUMPTO(ni);
			}

			ni = setup_frame(pc);
			JUMPTO(ni);	/* Op_func */
		}

		case Op_K_return:
			m = POP_SCALAR();       /* return value */

			ni = pop_fcall();
	
			/* put the return value back on stack */
			PUSH(m);

			JUMPTO(ni);

		case Op_K_getline_redir:
			if ((currule == BEGINFILE || currule == ENDFILE)
					&& pc->into_var == FALSE
					&& pc->redir_type == redirect_input)
				fatal(_("`getline' invalid inside `%s' rule"), ruletab[currule]);
			r = do_getline_redir(pc->into_var, pc->redir_type);
			PUSH(r);
			break;

		case Op_K_getline:	/* no redirection */
			if (! currule || currule == BEGINFILE || currule == ENDFILE)
				fatal(_("non-redirected `getline' invalid inside `%s' rule"),
						ruletab[currule]);

			do {
				int ret;
				ret = nextfile(& curfile, FALSE);
				if (ret <= 0)
					r = do_getline(pc->into_var, curfile);
				else {

					/* Save execution state so that we can return to it
					 * from Op_after_beginfile or Op_after_endfile.
					 */ 

					push_exec_state(pc, currule, source, stack_ptr);

					if (curfile == NULL)
						JUMPTO((pc + 1)->target_endfile);
					else
						JUMPTO((pc + 1)->target_beginfile);
				}
			} while (r == NULL);	/* EOF */

			PUSH(r);
			break;

		case Op_after_endfile:
			/* Find the execution state to return to */
			ni = pop_exec_state(& currule, & source, NULL);

			assert(ni->opcode == Op_newfile || ni->opcode == Op_K_getline);
			JUMPTO(ni);

		case Op_after_beginfile:
			after_beginfile(& curfile);

			/* Find the execution state to return to */
			ni = pop_exec_state(& currule, & source, NULL);

			assert(ni->opcode == Op_newfile || ni->opcode == Op_K_getline);
			if (ni->opcode == Op_K_getline
					|| curfile == NULL      /* skipping directory argument */
			)
				JUMPTO(ni);

			break;	/* read a record, Op_get_record */

		case Op_newfile:
		{
			int ret;

			ret = nextfile(& curfile, FALSE);

			if (ret < 0)	/* end of input */
				JUMPTO(pc->target_jmp);	/* end block or Op_atexit */

			if (ret == 0) /* read a record */
				JUMPTO((pc + 1)->target_get_record);

			/* ret > 0 */
			/* Save execution state for use in Op_after_beginfile or Op_after_endfile. */

			push_exec_state(pc, currule, source, stack_ptr);

			if (curfile == NULL)	/* EOF */
				JUMPTO(pc->target_endfile);
			/* else
				execute beginfile block */
		}
			break;
			
		case Op_get_record:		
		{
			int errcode = 0;

			ni = pc->target_newfile;
			if (curfile == NULL) {
				/* from non-redirected getline, e.g.:
				 *  {
				 *		while (getline > 0) ;
				 *  }
				 */

				ni = ni->target_jmp;	/* end_block or Op_atexit */
				JUMPTO(ni);
			}

			if (inrec(curfile, & errcode) != 0) {
				if (errcode > 0 && (do_traditional || ! pc->has_endfile))
					fatal(_("error reading input file `%s': %s"),
						curfile->name, strerror(errcode));

				JUMPTO(ni);
			} /* else
				prog (rule) block */
		}
			break;

		case Op_K_nextfile:
		{
			int ret;

			if (currule != Rule && currule != BEGINFILE)
				fatal(_("`nextfile' cannot be called from a `%s' rule"),
					ruletab[currule]);

			ret = nextfile(& curfile, TRUE);	/* skip current file */

			if (currule == BEGINFILE) {
				long stack_size;

				ni = pop_exec_state(& currule, & source, & stack_size);

				assert(ni->opcode == Op_K_getline || ni->opcode == Op_newfile);

				/* pop stack returning to the state of Op_K_getline or Op_newfile. */
				unwind_stack(stack_size);

				if (ret == 0) {
					/* There was an error opening the file;
					 * don't run ENDFILE block(s).
					 */

					JUMPTO(ni);
				} else {
					/* do run ENDFILE block(s) first. */
					
					/* Execution state to return to in Op_after_endfile. */
					push_exec_state(ni, currule, source, stack_ptr);

					JUMPTO(pc->target_endfile);
				}				
			} /* else 
				Start over with the first rule. */

			/* empty the run-time stack to avoid memory leak */
			pop_stack();

			/* Push an execution state for Op_after_endfile to return to */
			push_exec_state(pc->target_newfile, currule, source, stack_ptr);

			JUMPTO(pc->target_endfile);
		}
			break;

		case Op_K_exit:
			/* exit not allowed in user-defined comparison functions for "sorted_in";
			 * This is done so that END blocks aren't executed more than once.
			 */
			if (! currule)
				fatal(_("`exit' cannot be called in the current context"));

			exiting = TRUE;
			t1 = POP_SCALAR();
			(void) force_number(t1);
			exit_val = (int) get_number_si(t1);
			DEREF(t1);
#ifdef VMS
			if (exit_val == 0)
				exit_val = EXIT_SUCCESS;
			else if (exit_val == 1)
				exit_val = EXIT_FAILURE;
			/* else
				just pass anything else on through */
#endif

			if (currule == BEGINFILE || currule == ENDFILE) {

				/* Find the rule of the saved execution state (Op_K_getline/Op_newfile).
				 * This is needed to prevent multiple execution of any END rules:
				 * 	gawk 'BEGINFILE { exit(1) } \
				 *         END { while (getline > 0); }' in1 in2
				 */

				(void) pop_exec_state(& currule, & source, NULL);
			}

			pop_stack();	/* empty stack, don't leak memory */

			/* Jump to either the first END block instruction
			 * or to Op_atexit.
			 */

			if (currule == END)
				ni = pc->target_atexit;
			else
				ni = pc->target_end;
			JUMPTO(ni);

		case Op_K_next:
			if (currule != Rule)
				fatal(_("`next' cannot be called from a `%s' rule"), ruletab[currule]);

			pop_stack();
			JUMPTO(pc->target_jmp);	/* Op_get_record, read next record */

		case Op_pop:
			r = POP_SCALAR();
			DEREF(r);
			break;

		case Op_line_range:
			if (pc->triggered)		/* evaluate right expression */
				JUMPTO(pc->target_jmp);
			/* else
				evaluate left expression */
			break;

		case Op_cond_pair:
		{
			int result;
			INSTRUCTION *ip;

			t1 = TOP_SCALAR();   /* from right hand side expression */
			di = (eval_condition(t1) != 0);
			DEREF(t1);

			ip = pc->line_range;            /* Op_line_range */

			if (! ip->triggered && di) {
				/* not already triggered and left expression is TRUE */
				decr_sp();
				ip->triggered = TRUE;
				JUMPTO(ip->target_jmp);	/* evaluate right expression */ 
			}

			result = ip->triggered || di;
			ip->triggered ^= di;          /* update triggered flag */
			r = node_Boolean[result];      /* final value of condition pair */
			UPREF(r);
			REPLACE(r);
			JUMPTO(pc->target_jmp);
		}

		case Op_exec_count:
			if (do_profile)
				pc->exec_count++;
			break;

		case Op_no_op:
		case Op_K_do:
		case Op_K_while:
		case Op_K_for:
		case Op_K_arrayfor:
		case Op_K_switch:
		case Op_K_default:
		case Op_K_if:
		case Op_K_else:
		case Op_cond_exp:
			break;

		default:
			fatal(_("Sorry, don't know how to interpret `%s'"), opcode2str(op));
		}

		JUMPTO(pc->nexti);

/*	} forever */

	/* not reached */
	return 0;

#undef mk_sub
#undef JUMPTO
}

#undef NV
