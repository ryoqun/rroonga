# Copyright (C) 2009  Kouhei Sutou <kou@clear-code.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License version 2.1 as published by the Free Software Foundation.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

class ColumnTest < Test::Unit::TestCase
  include GroongaTestUtils

  def setup
    setup_database

    setup_users_table
    setup_bookmarks_table
    setup_indexes
  end

  def setup_users_table
    @users_path = @tables_dir + "users"
    @users = Groonga::Array.create(:name => "users",
                                   :path => @users_path.to_s)

    @users_name_column_path = @columns_dir + "name"
    @users_name_column =
      @users.define_column("name", "<shorttext>",
                           :path => @users_name_column_path.to_s,
                           :type => "vector")
  end

  def setup_bookmarks_table
    @bookmarks_path = @tables_dir + "bookmarks"
    @bookmarks = Groonga::Array.create(:name => "bookmarks",
                                       :path => @bookmarks_path.to_s)

    @uri_column_path = @columns_dir + "uri"
    @bookmarks_uri = @bookmarks.define_column("uri", "<shorttext>",
                                              :path => @uri_column_path.to_s)

    @comment_column_path = @columns_dir + "comment"
    @bookmarks_comment =
      @bookmarks.define_column("comment", "<text>",
                               :path => @comment_column_path.to_s)

    @content_column_path = @columns_dir + "content"
    @bookmarks_content = @bookmarks.define_column("content", "<longtext>")

    @user_id_column_path = @columns_dir + "user-id"
    @bookmarks_user_id = @bookmarks.define_column("user_id", @users)
  end

  def setup_indexes
    @bookmarks_index_path = @tables_dir + "bookmarks-index"
    @bookmarks_index =
      Groonga::PatriciaTrie.create(:name => "bookmarks-index",
                                   :path => @bookmarks_index_path.to_s)
    @content_index_column_path = @columns_dir + "content-index"
    @bookmarks_index_content =
      @bookmarks_index.define_column("<index:content>", @bookmarks,
                                     :type => "index",
                                     :with_section => true,
                                     :with_weight => true,
                                     :with_position => true,
                                     :path => @content_index_column_path.to_s)

    @uri_index_column_path = @columns_dir + "uri-index"
    @bookmarks_uri_index =
      @bookmarks_index.define_column("<index:uri>", @bookmarks,
                                     :type => "index",
                                     :path => @uri_index_column_path.to_s)
  end

  def test_source_info
    @bookmarks_index_content.sources = [@bookmarks_content]

    groonga = @bookmarks.add
    groonga["content"] = "<html><body>groonga</body></html>"

    ruby = @bookmarks.add
    ruby["content"] = "<html><body>ruby</body></html>"

    assert_content_search([groonga], "groonga")
    assert_content_search([ruby, groonga], "html")

    assert_equal([@bookmarks_content], @bookmarks_index_content.sources)
  end

  def test_update_index_column
    groonga = @bookmarks.add
    groonga["content"] = "<html><body>groonga</body></html>"

    ruby = @bookmarks.add
    ruby["content"] = "<html><body>ruby</body></html>"

    @bookmarks_index_content[groonga.id] = groonga["content"]
    @bookmarks_index_content[ruby.id] = ruby["content"]

    assert_content_search([groonga], "groonga")
    assert_content_search([ruby, groonga], "html")
  end

  def test_range
    assert_equal(context[Groonga::Type::SHORT_TEXT], @bookmarks_uri.range)
    assert_equal(context[Groonga::Type::TEXT], @bookmarks_comment.range)
    assert_equal(context[Groonga::Type::LONG_TEXT], @bookmarks_content.range)
    assert_equal(@users, @bookmarks_user_id.range)
    assert_equal(@bookmarks, @bookmarks_index_content.range)
    assert_equal(@bookmarks, @bookmarks_uri_index.range)
  end

  def test_accessor
    bookmark = @bookmarks.add
    bookmark["uri"] = "http://groonga.org/"
    @bookmarks_uri_index[bookmark.id] = "http://groonga.org/"
    result = @bookmarks_uri_index.search("http://groonga.org/")
    accessor = result.column(".<index:uri>.uri")
    assert_equal(["http://groonga.org/"],
                 result.records.collect {|record| accessor[record.id]})
  end

  def test_inspect
    assert_equal("#<Groonga::VarSizeColumn " +
                 "id: <#{@users_name_column.id}>, " +
                 "name: <users.name>, " +
                 "path: <#{@users_name_column_path}>, " +
                 "domain: <#{@users.inspect}>, " +
                 "range: <#{context['<shorttext>'].inspect}>" +
                 ">",
                 @users_name_column.inspect)
  end

  def test_domain
    assert_equal(@users, @users_name_column.domain)
  end

  private
  def assert_content_search(expected_records, term)
    records = @bookmarks_index_content.search(term).records
    expected_contents = expected_records.collect do |record|
      record["content"]
    end
    actual_contents = records.collect do |record|
      record[".<index:content>.content"]
    end
    assert_equal(expected_contents, actual_contents)
  end
end