#include <stdio.h>

int
ungetc(int c, FILE *iop)
{
	if (c == EOF || (iop->_flag & (_IOREAD|_IORW)) == 0 ||
	    iop->_ptr == NULL || iop->_base == NULL)
		return (EOF);

	/*
	 * sscanf() uses a read-only string as the backing store for an
	 * _IOSTRG stream.  The scanner only pushes back the byte it has just
	 * read, so moving the cursor back is sufficient and avoids writing
	 * through a pointer which may refer to a string literal.
	 */
	if (iop->_flag & _IOSTRG) {
		if (iop->_ptr <= iop->_base ||
		    (unsigned char)iop->_ptr[-1] != (unsigned char)c)
			return (EOF);
		iop->_ptr--;
		iop->_cnt++;
		iop->_flag &= ~_IOEOF;
		return ((unsigned char)c);
	}

	if (iop->_ptr == iop->_base) {
		if (iop->_cnt == 0)
			iop->_ptr++;
		else
			return (EOF);
        }

	iop->_cnt++;
	*--iop->_ptr = c;
	iop->_flag &= ~_IOEOF;

	return (c);
}
