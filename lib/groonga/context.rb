# -*- coding: utf-8 -*-
#
# Copyright (C) 2010-2011  Kouhei Sutou <kou@clear-code.com>
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

require "groonga/json"

module Groonga
  class Context
    # _path_ にある既存のデータベースを開く。ブロックを指定した場
    # 合はブロックに開いたデータベースを渡し、ブロックを抜けると
    # きに閉じる。
    def open_database(path, &block)
      options = {:context => self}

      Database.open(path, options, &block)
    end

    # _path_ に新しくデータベースを作成する。 _path_ を省略すると
    # 一時データベースとなる。
    #
    # @example
    #   #一時データベースを作成:
    #   context.create_database
    #
    #   #永続データベースを作成:
    #   context.create_database("/tmp/db.groonga")
    def create_database(path=nil)
      options = {:context => self}
      if path
        options[:path] = path
      end

      Database.create(options)
    end

    # groongaのプラグインディレクトリにあるプラグイン _name_
    # を登録する。 _path_ を指定するとプラグインディレクトリ以
    # 外にあるプラグインを登録することができる。
    def register_plugin(name_or_options)
      options = {:context => self}
      if name_or_options.is_a?(String)
        name = name_or_options
        Plugin.register(name, options)
      else
        Plugin.register(name_or_options.merge(options))
      end
    end

    # _table_ から指定した条件にマッチするレコードの値を取得
    # する。 _table_ はテーブル名かテーブルオブジェクトを指定
    # する。
    #
    # _options_ に指定できるキーは以下の通り。
    # @param [::Hash] options The name and value
    #   pairs. Omitted names are initialized as the default value.
    # @option options [Array] output_columns The output_columns
    #
    #   値を取得するカラムを指定する。
    # @option options [Array] XXX TODO
    #   TODO
    def select(table, options={})
      select = SelectCommand.new(self, table, options)
      select.exec
    end

    class SelectResult < Struct.new(:n_hits, :columns, :values,
                                    :drill_down)
      class << self
        def parse(json, drill_down_keys)
          select_result, *drill_down_results = JSON.parse(json)
          result = new
          n_hits, columns, values = extract_result(select_result)
          result.n_hits = n_hits
          result.columns = columns
          result.values = values
          if drill_down_results
            result.drill_down = parse_drill_down_results(drill_down_results,
                                                         drill_down_keys)
          end
          result
        end

        def create_records(columns, values)
          records = []
          values.each do |value|
            record = {}
            columns.each_with_index do |(name, type), i|
              record[name] = convert_value(value[i], type)
            end
            records << record
          end
          records
        end

        private
        def parse_drill_down_results(results, keys)
          named_results = {}
          results.each_with_index do |drill_down, i|
            n_hits, columns, values = extract_result(drill_down)
            drill_down_result = DrillDownResult.new
            drill_down_result.n_hits = n_hits
            drill_down_result.columns = columns
            drill_down_result.values = values
            named_results[keys[i]] = drill_down_result
          end
          named_results
        end

        def extract_result(result)
          meta_data, columns, *values = result
          n_hits, = meta_data
          [n_hits, columns, values]
        end

        def convert_value(value, type)
          case type
          when "Time"
            Time.at(value)
          else
            value
          end
        end
      end

      def records
        @records ||= self.class.create_records(columns, values)
      end

      class DrillDownResult < Struct.new(:n_hits, :columns, :values)
        def records
          @records ||= SelectResult.create_records(columns, values)
        end
      end
    end

    class SelectCommand
      def initialize(context, table, options)
        @context = context
        @table = table
        @options = normalize_options(options)
      end

      def exec
        request_id = @context.send(query)
        loop do
          response_id, result = @context.receive
          if request_id == response_id
            drill_down_keys = @options["drilldown"]
            if drill_down_keys.is_a?(String)
              drill_down_keys = drill_down_keys.split(/(?:\s+|\s*,\s*)/)
            end
            return SelectResult.parse(result, drill_down_keys)
          end
          # raise if request_id < response_id
        end
      end

      private
      def normalize_options(options)
        normalized_options = {}
        options.each do |key, value|
          normalized_options[normalize_option_name(key)] = value
        end
        normalized_options
      end

      def normalize_option_name(name)
        name = name.to_s.gsub(/-/, "_").gsub(/drill_down/, "drilldown")
        name.gsub(/sort_by/, 'sortby')
      end

      def query
        if @table.is_a?(String)
          table_name = @table
        else
          table_name = @table.name
        end
        _query = "select #{table_name}"
        @options.each do |key, value|
          value = value.join(", ") if value.is_a?(::Array)
          escaped_value = value.to_s.gsub(/"/, '\\"')
          _query << " --#{key} \"#{escaped_value}\""
        end
        _query
      end
    end
  end
end
