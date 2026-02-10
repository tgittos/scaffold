"""Fetch web page content.

Gate: network
Match: url

Security Note: This tool intentionally allows fetching from localhost and
internal network resources. This is a development tool where the agent may
need to test local services, access development servers, or fetch from
internal resources. SSRF protections are deliberately not implemented.
The security boundary is the user's decision to run ralph with network access.
"""

import html.parser
import re


MAX_CONTENT_SIZE = 1024 * 1024  # 1MB

def web_fetch(url: str, timeout: int = 30) -> dict:
    """Fetch web page content.

    Args:
        url: URL to fetch
        timeout: Request timeout in seconds (default: 30)

    Returns:
        Dictionary with url, content, content_type, size, and truncated flag
    """
    import _ralph_http

    # Validate URL
    if not url.startswith(('http://', 'https://')):
        raise ValueError("URL must start with http:// or https://")

    headers = [
        'User-Agent: Ralph/1.0 (Text-based web browser)',
        'Accept: text/html,application/xhtml+xml,text/plain,*/*',
    ]

    try:
        result = _ralph_http.get(url, headers=headers, timeout=timeout)
    except Exception as e:
        raise Exception(f"Fetch failed: {str(e)}")

    if not result.get("ok"):
        status = result.get("status", 0)
        if status > 0:
            raise Exception(f"HTTP Error {status}")
        else:
            raise Exception("Fetch failed: connection error")

    data = result.get("data", "")
    content_type = result.get("content_type", "")

    # Extract charset from Content-Type header
    charset = 'utf-8'
    if 'charset=' in content_type:
        charset = content_type.split('charset=')[-1].split(';')[0].strip()

    # Re-encode with proper charset if not UTF-8
    content = data
    if charset.lower() not in ('utf-8', 'utf8', ''):
        try:
            content = data.encode('utf-8').decode(charset, errors='replace')
        except (UnicodeDecodeError, LookupError):
            pass

    # Convert HTML to plain text if it's HTML content
    if 'html' in content_type.lower() or content.strip().startswith('<!DOCTYPE') or content.strip().startswith('<html'):
        content = html_to_text(content)

    # Truncate very long content
    truncated = False
    if len(content) > MAX_CONTENT_SIZE:
        content = content[:MAX_CONTENT_SIZE] + '\n[Content truncated at 1MB]'
        truncated = True

    return {
        "url": url,
        "content": content,
        "content_type": content_type,
        "size": len(content),
        "truncated": truncated
    }


class _HTMLTextExtractor(html.parser.HTMLParser):
    """Simple HTML to text converter."""

    def __init__(self):
        super().__init__()
        self.result = []
        self.skip_tags = {'script', 'style', 'head', 'meta', 'link', 'noscript'}
        self.block_tags = {'p', 'div', 'br', 'li', 'tr', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6',
                          'article', 'section', 'header', 'footer', 'nav', 'aside'}
        self.current_skip = 0

    def handle_starttag(self, tag, attrs):
        if tag in self.skip_tags:
            self.current_skip += 1
        elif tag in self.block_tags and self.result:
            self.result.append('\n')
        elif tag == 'a':
            # Try to extract href
            for name, value in attrs:
                if name == 'href' and value and not value.startswith('#'):
                    self.result.append(' [')

    def handle_endtag(self, tag):
        if tag in self.skip_tags:
            self.current_skip = max(0, self.current_skip - 1)
        elif tag in self.block_tags:
            self.result.append('\n')
        elif tag == 'a':
            self.result.append('] ')

    def handle_data(self, data):
        if self.current_skip == 0:
            text = data.strip()
            if text:
                self.result.append(text + ' ')

    def get_text(self):
        text = ''.join(self.result)
        # Clean up whitespace
        text = re.sub(r'\n\s*\n+', '\n\n', text)
        text = re.sub(r' +', ' ', text)
        return text.strip()


def html_to_text(html_content: str) -> str:
    """Convert HTML to plain text."""
    parser = _HTMLTextExtractor()
    try:
        parser.feed(html_content)
        return parser.get_text()
    except Exception:
        # Fallback: strip tags with regex
        text = re.sub(r'<script[^>]*>.*?</script>', '', html_content, flags=re.DOTALL | re.IGNORECASE)
        text = re.sub(r'<style[^>]*>.*?</style>', '', text, flags=re.DOTALL | re.IGNORECASE)
        text = re.sub(r'<[^>]+>', ' ', text)
        text = re.sub(r'\s+', ' ', text)
        return text.strip()
